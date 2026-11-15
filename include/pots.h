#pragma once
#include <stdint.h>
void pots_init(void);
/* Eight-sample averages expanded to the reference's full 0..65535 range. */
void pots_read(uint16_t *p1, uint16_t *p2);
void pots_print_status(void);
