/* SSD1306 128x64, PIO0 SM1 I2C SDA GP15 / SCL GP14. No DMA/IRQ resources; audio IRQs stay enabled.
 * Page addressing commands follow the Solomon Systech SSD1306 datasheet.
 * Each task sends at most 16 pixels-columns (plus address commands), bounded
 * by two 2 ms I2C timeouts. Missing/faulty displays disable themselves. */
#include "oled.h"
#include "oled_font.h"
#include "oled_logo.h"
#include "pvs_config.h"
#include "pico/stdlib.h"
#include "oled_i2c.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t frame[1024], sent[1024];
static uint8_t address;
static bool online;
static bool inverted, sent_inverted;
static int last_result, probe_result[2];
static const char *stage = "not initialized";
static unsigned chunks_sent;
static unsigned cursor, failures;
static uint32_t last_transfer;
static bool splash_active, splash_started;
static uint32_t splash_start;
bool oled_copy_display(uint8_t pixels[1024], bool *polarity) {
    memcpy(pixels, online ? sent : frame, sizeof sent);
    *polarity = online ? sent_inverted : inverted;
    return online;
}
#define SPLASH_DURATION_US 3000000u
static const oled_glyph_t *glyph(unsigned char c) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c < 32 || c > 126) c = '?';
    return &oled_font[c - 32];
}
static void draw_line(unsigned y, const char *text) {
    if (!text) return;
    unsigned width=0, count=0;
    /* Bound both reading and drawing even for an unexpectedly long label. */
    while (count<32 && text[count]) width += glyph((unsigned char)text[count++])->width;
    unsigned x = width < 128 ? (128-width)/2 : 0;
    for (unsigned i=0;i<count && x<128;++i) {
        const oled_glyph_t *g = glyph((unsigned char)text[i]);
        for (unsigned col=0;col<g->width && x<128;++col,++x)
            for (unsigned bit=0;bit<16 && y+bit<64;++bit)
                if (g->columns[col] & (1u<<bit))
                    frame[((y+bit)/8)*128+x] |= (uint8_t)(1u<<((y+bit)%8));
    }
}
void oled_message(const char *top, const char *middle, const char *bottom) {
    if (splash_active) return;
    inverted = false;
    memset(frame,0,sizeof frame);
    draw_line(2,top);
    draw_line(24,middle);
    draw_line(46,bottom);
}
void oled_flash_screen(unsigned build) {
    char label[24];
    snprintf(label,sizeof label,"BUILD %03u",build);
    splash_active = false;
    oled_message("FIRMWARE","FLASH",label);
}
/* A7/A6 invert the panel without rewriting its 1024-byte display RAM. */
void oled_notice(const char *top, const char *middle) {
    if (splash_active) return;
    oled_message(top,middle,"");
    inverted = true;
}
void oled_show(const char *mode, const char *pot1, const char *pot2) {
    oled_message(mode,pot2,pot1);
}
static bool write_bytes(const uint8_t *data, size_t size) {
    last_result = oled_i2c_write(address, data, size, 2000);
    return last_result == (int)size;
}
void oled_init(void) {
    stage = "PIO init";
    if (!oled_i2c_init()) return;
    /* Probe the two SSD1306 addresses with display-off (no bus-wide scan). */
    const uint8_t probe[] = {0, 0xae};
    stage = "probe";
    for (address=0x3c; address<=0x3d; ++address) {
        bool found = write_bytes(probe,sizeof probe);
        probe_result[address-0x3c] = last_result;
        if (found) { online=true; break; }
    }
    if (!online) return;
    const uint8_t setup[] = {0,0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,
        /* Rotate 180 degrees from the prototype mounting, for the new PCB. */
        0x8d,0x14,0x20,0x02,0xa1,0xc8,0xda,0x12,0x81,0x7f,
        0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0x2e,0xaf};
    stage = "setup";
    if (!write_bytes(setup,sizeof setup)) { online=false; ++failures; return; }
    stage = "ready";
    memcpy(frame,oled_boot_logo,sizeof frame);
    /* Complement every byte so all initial chunks are sent, even solid ones. */
    for (unsigned i=0;i<sizeof sent;++i) sent[i]=(uint8_t)~frame[i];
    splash_active=true;
    splash_started=false;
}
void oled_task(void) {
    uint32_t now = time_us_32();
    if (!online) return;
    if (splash_active && splash_started &&
        (uint32_t)(now-splash_start)>=SPLASH_DURATION_US)
        splash_active=false; /* The next UI refresh draws the current mode. */
    /* Prioritize a polarity change over pixel chunks and their pacing delay.
     * One short command changes the whole background without a visible wipe. */
    if (inverted != sent_inverted) {
        const uint8_t command[] = {0, inverted ? 0xa7 : 0xa6};
        stage = "inversion";
        if (!write_bytes(command,sizeof command)) {
            online=false; ++failures; return;
        }
        sent_inverted = inverted;
        stage = "ready";
        last_transfer = time_us_32();
        return;
    }
    if ((uint32_t)(now-last_transfer) < 1000u) return;
    for (unsigned checked=0; checked<64; ++checked) {
        unsigned block=cursor;
        cursor=(cursor+1)%64;
        unsigned offset=block*16;
        if (!memcmp(frame+offset,sent+offset,16)) continue;
        uint8_t col=(uint8_t)(offset%128);
        uint8_t command[]={0,(uint8_t)(0xb0+offset/128),(uint8_t)(col&15),(uint8_t)(0x10+(col>>4))};
        uint8_t data[17]={0x40};
        memcpy(data+1,frame+offset,16);
        stage = "page address";
        if (!write_bytes(command,sizeof command)) {
            online=false; ++failures; return;
        }
        stage = "pixel data";
        if (!write_bytes(data,sizeof data)) {
            online=false; ++failures; return;
        }
        ++chunks_sent;
        stage = "ready";
        memcpy(sent+offset,data+1,16);
        last_transfer=time_us_32();
        return;
    }
    /* Start the hold only after the complete logo has reached the panel. */
    if (splash_active && !splash_started) {
        splash_start=now;
        splash_started=true;
    }
}
void oled_print_status(void) {
    printf("OLED: SSD1306 128x64 PIO0 SM1 I2C SDA GP%d SCL GP%d, %s, address 0x%02x, errors%u\n",
           PVS_OLED_SDA,PVS_OLED_SCL,online?"online":"offline",address<=0x3d?address:0,failures);
    printf("OLED diagnostic: stage=%s result=%d probe3C=%d probe3D=%d chunks=%u\n",
           stage,last_result,probe_result[0],probe_result[1],chunks_sent);
}
