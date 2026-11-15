#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Core 0 only. Copy panel state, or the virtual frame if no OLED is online.
 * Returns whether a physical display is online; pixels are valid either way. */
bool oled_copy_display(uint8_t pixels[1024], bool *inverted);
void oled_init(void);
void oled_show(const char *mode, const char *pot1, const char *pot2);
/* Top-to-bottom lines for messages, independent of physical pot order. */
void oled_message(const char *top, const char *middle, const char *bottom);
void oled_task(void);
void oled_print_status(void);

void oled_notice(const char *top, const char *middle);

/* Override the boot splash for an imminent firmware reboot. */
void oled_flash_screen(unsigned build);
