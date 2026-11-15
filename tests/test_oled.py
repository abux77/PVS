"""Exercise actual OLED driver: bounded transfers, dirty tracking and failures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pvs-oled-test-') as tmp:
    path = Path(tmp)
    (path/'pico').mkdir(); (path/'hardware').mkdir()
    (path/'pico/stdlib.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
#define GPIO_FUNC_I2C 3
extern uint32_t fake_time;
static inline uint32_t time_us_32(void) {return fake_time;}
static inline void gpio_set_function(unsigned p,unsigned f) {(void)p;(void)f;}
static inline void gpio_pull_up(unsigned p) {(void)p;}
''')
    (path/'test.c').write_text('''#include "oled.h"
#include "oled_font.h"
#include "oled_logo.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
uint32_t fake_time;
static unsigned calls, data_calls, page, column;
static uint8_t ram[1024];
static int mode;
static bool fail, inverted;
bool oled_i2c_init(void) {return mode!=3;}
int oled_i2c_write(uint8_t addr,const uint8_t *data,size_t n,unsigned timeout) {
    ++calls;
    assert(timeout==2000 && n<=28);
    if (mode==1 || fail || (mode==2 && addr==0x3c)) return -1;
    if (n==2 && data[0]==0 && (data[1]==0xa7 || data[1]==0xa6)) inverted=data[1]==0xa7;
    if (n==4 && data[0]==0) {
        page=data[1]-0xb0;
        column=(data[2]&15) | ((data[3]&15)<<4);
    }
    if (data[0]==0x40) {
        assert(n==17 && page<8 && column<=112);
        memcpy(ram+page*128+column,data+1,16);
        ++data_calls;
    }
    return (int)n;
}
int main(int argc,char **argv) {
    assert(argc==2);mode=argv[1][0]-'0';
    for(unsigned c='0';c<='9';++c) assert(oled_font[c-32].width==oled_font['0'-32].width);
    oled_init();
    unsigned initial=calls;
    oled_show("NORMAL","SPEED 255","PITCH 255");
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    if (mode==1 || mode==3) {
        assert(calls==initial && data_calls==0);
        uint8_t virtual_frame[1024]; bool polarity;
        assert(!oled_copy_display(virtual_frame,&polarity) && !polarity);
        unsigned lit=0; for(unsigned i=0;i<1024;++i) if(virtual_frame[i]) ++lit;
        assert(lit>100);
        oled_notice("PRESET","LOADED");
        assert(!oled_copy_display(virtual_frame,&polarity) && polarity);
        assert(calls==initial);
        return 0;
    }
    assert(data_calls==64);
    assert(!memcmp(ram,oled_boot_logo,sizeof ram));
    uint8_t snapshot[1024]; bool polarity;
    assert(oled_copy_display(snapshot,&polarity));
    assert(!polarity && !memcmp(snapshot,ram,sizeof ram));
    oled_notice("BANK 3","LOADED");
    fake_time=3000000;oled_task();
    assert(!inverted && !memcmp(ram,oled_boot_logo,sizeof ram));
    fake_time+=100000;oled_task();
    oled_show("NORMAL","SPEED 255","PITCH 255");
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    assert(memcmp(ram,oled_boot_logo,sizeof ram));
    unsigned lit=0;
    for(unsigned i=0;i<1024;++i) if(ram[i]) ++lit;
    assert(lit>100);
    unsigned steady=calls;
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    assert(calls==steady);
    oled_show("NORMAL","SPEED 255","PITCH 255");fake_time+=1000;oled_task();
    assert(calls==steady);
    oled_message("PRESET","LOADED","");
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    uint8_t normal[1024]; memcpy(normal,ram,sizeof normal);
    unsigned previous_calls=calls, previous_data=data_calls;
    oled_notice("PRESET","LOADED");
    oled_task(); /* No time advance: inversion bypasses chunk pacing. */
    assert(inverted && calls==previous_calls+1 && data_calls==previous_data);
    assert(oled_copy_display(snapshot,&polarity));
    assert(polarity && !memcmp(snapshot,ram,sizeof ram));
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    assert(!memcmp(normal,ram,sizeof normal) && data_calls==previous_data);
    previous_calls=calls;
    oled_message("PRESET","LOADED","");
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    assert(!inverted && calls==previous_calls+1 && data_calls==previous_data);
    assert(!memcmp(normal,ram,sizeof normal));
    steady=calls;
    oled_show("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXEXCESS",NULL,"");fail=true;
    fake_time+=1000;oled_task();
    assert(calls==steady+1);
    for (unsigned i=0;i<100;++i) {fake_time+=1000;oled_task();}
    assert(calls==steady+1);
    assert(!oled_copy_display(snapshot,&polarity));
}
''')
    exe = path/'test'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+tmp,'-I'+str(root/'include'),'-I'+str(root/'src'),str(root/'src/oled.c'),str(path/'test.c'),'-o',str(exe)],check=True)
    for mode in ('0','1','2','3'):
        subprocess.run([str(exe),mode],check=True)
print('PASS: OLED dirty updates, bounds, both addresses and absent/failing bus')
