"""Check OLED PIO transport byte order, ACK/NACK, timeout and line release."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pvs-oled-bus-') as tmp:
    path = Path(tmp)
    (path/'pico').mkdir()
    (path/'hardware').mkdir()
    (path/'pico/stdlib.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
uint32_t time_us_32(void);
void busy_wait_us_32(uint32_t);
bool gpio_get(unsigned);
void gpio_pull_up(unsigned);
static inline void tight_loop_contents(void) {}
''')
    (path/'hardware/clocks.h').write_text('''#pragma once
#define clk_sys 0
static inline unsigned clock_get_hz(int c) {(void)c;return 125000000;}
''')
    (path/'oled_i2c.pio.h').write_text('''#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef unsigned pio_sm_config;
#define pio0 0
static const int pvs_oled_i2c_program=0;
#define pvs_oled_i2c_program_get_default_config(o) (o)
#define sm_config_set_out_pins(c,p,n) ((void)(c),(void)(p),(void)(n))
#define sm_config_set_in_pins(c,p) ((void)(c),(void)(p))
#define sm_config_set_sideset_pins(c,p) ((void)(c),(void)(p))
#define sm_config_set_jmp_pin(c,p) ((void)(c),(void)(p))
#define sm_config_set_out_shift(c,a,b,n) ((void)(c),(void)(a),(void)(b),(void)(n))
#define sm_config_set_in_shift sm_config_set_out_shift
#define sm_config_set_clkdiv(c,d) ((void)(c),(void)(d))
#define pio_encode_jmp(o) (o)
#define pio_encode_sideset(n,v) ((v)<<12)
bool pio_sm_is_claimed(int,unsigned);
bool pio_can_add_program(int,const int*);
void pio_sm_claim(int,unsigned);
unsigned pio_add_program(int,const int*);
void pio_sm_init(int,unsigned,unsigned,const pio_sm_config*);
void pio_sm_set_pins_with_mask(int,unsigned,uint32_t,uint32_t);
void pio_sm_set_pindirs_with_mask(int,unsigned,uint32_t,uint32_t);
void pio_sm_set_enabled(int,unsigned,bool);
void pio_sm_clear_fifos(int,unsigned);
void pio_sm_restart(int,unsigned);
void pio_sm_exec(int,unsigned,unsigned);
void pio_gpio_init(int,unsigned);
void pio_sm_put(int,unsigned,uint32_t);
bool pio_sm_is_rx_fifo_empty(int,unsigned);
uint32_t pio_sm_get(int,unsigned);
''')
    (path/'test.c').write_text('''#include <assert.h>
#include "pvs_config.h"
#include "oled_i2c.pio.h"
#include "oled_i2c.h"
static uint32_t now, dirs, result;
static unsigned bits, nack_byte, block_bit;
static uint8_t bytes[32];
static bool enabled, stuck_clock, stuck_data, claimed;
uint32_t time_us_32(void) {return now++;}
void busy_wait_us_32(uint32_t n) {now+=n;}
bool gpio_get(unsigned pin) {return pin==PVS_OLED_SCL ? !stuck_clock : !stuck_data;}
void gpio_pull_up(unsigned p) {assert(p==14 || p==15);}
bool pio_sm_is_claimed(int p,unsigned s) {(void)p;assert(s==1);return claimed;}
bool pio_can_add_program(int p,const int *x) {(void)p;(void)x;return true;}
void pio_sm_claim(int p,unsigned s) {(void)p;assert(s==1);claimed=true;}
unsigned pio_add_program(int p,const int *x) {(void)p;(void)x;return 8;}
void pio_sm_init(int p,unsigned s,unsigned o,const pio_sm_config *c) {(void)p;(void)s;(void)o;(void)c;}
void pio_sm_set_pins_with_mask(int p,unsigned s,uint32_t v,uint32_t m) {(void)p;(void)s;(void)m;assert(v==0);}
void pio_sm_set_pindirs_with_mask(int p,unsigned s,uint32_t v,uint32_t m) {(void)p;(void)s;assert(m==((1u<<14)|(1u<<15)));dirs=v;}
void pio_sm_set_enabled(int p,unsigned s,bool e) {(void)p;(void)s;enabled=e;}
void pio_sm_clear_fifos(int p,unsigned s) {(void)p;(void)s;}
void pio_sm_restart(int p,unsigned s) {(void)p;(void)s;}
void pio_sm_exec(int p,unsigned s,unsigned i) {(void)p;(void)s;assert(i==(8|4096));}
void pio_gpio_init(int p,unsigned s) {(void)p;assert(s==14 || s==15);}
void pio_sm_put(int p,unsigned s,uint32_t v) {
    (void)p;(void)s;assert(enabled && v<=1);
    unsigned n=bits/9, b=bits%9;assert(n<32);
    if(b<8) bytes[n]=(uint8_t)((bytes[n]<<1)|!v);
    else assert(v==0); /* release SDA for slave ACK */
    result=b==8 && nack_byte==n+1;
    ++bits;
}
bool pio_sm_is_rx_fifo_empty(int p,unsigned s) {(void)p;(void)s;return block_bit && bits>=block_bit;}
uint32_t pio_sm_get(int p,unsigned s) {(void)p;(void)s;return result;}
static void clear(void) {bits=0;nack_byte=0;block_bit=0;stuck_clock=false;stuck_data=false;}
int main(void) {
    const uint8_t data[]={0x00,0xa5,0xff};
    assert(oled_i2c_write(0x3c,data,3,2000)<0);
    assert(oled_i2c_init());
    assert(oled_i2c_write(0x3c,data,3,2000)==3);
    assert(bits==36 && bytes[0]==0x78 && bytes[1]==0 && bytes[2]==0xa5 && bytes[3]==0xff);
    assert(!enabled && dirs==0);
    clear();nack_byte=1;
    assert(oled_i2c_write(0x3d,data,3,2000)<0 && bits==9 && bytes[0]==0x7a);
    clear();nack_byte=3;
    assert(oled_i2c_write(0x3c,data,3,2000)<0 && bits==27);
    clear();block_bit=5;uint32_t start=now;
    assert(oled_i2c_write(0x3c,data,3,100)<0 && now-start<130);
    assert(!enabled && dirs==0);
    clear();stuck_clock=true;start=now;
    assert(oled_i2c_write(0x3c,data,3,100)<0 && bits==0 && now-start<130);
    assert(dirs==0);
    clear();stuck_data=true;
    assert(oled_i2c_write(0x3c,data,3,100)<0 && bits==0 && dirs==0);
    clear();assert(oled_i2c_write(0x3c,data,3,2000)==3);
    assert(oled_i2c_write(0x80,data,3,2000)<0);
    assert(oled_i2c_write(0x3c,0,3,2000)<0);
}
''')
    exe=path/'test'
    subprocess.run(shlex.split(os.environ.get('CC','cc'))+[
        '-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+tmp,'-I'+str(root/'include'),str(root/'src/oled_i2c.c'),
        str(path/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: PIO I2C serialization, ACK/NACK, bounded faults and recovery')
