#pragma once
void oled_init(void);
void oled_show(const char *mode, const char *pot1, const char *pot2);
/* Top-to-bottom lines for messages, independent of physical pot order. */
void oled_message(const char *top, const char *middle, const char *bottom);
void oled_task(void);
void oled_print_status(void);

void oled_notice(const char *top, const char *middle);

/* Override the boot splash for an imminent firmware reboot. */
void oled_flash_screen(unsigned build);
