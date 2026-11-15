#pragma once
#include <stdint.h>
/* Logical phrase/preset order follows the printed keys, then mode keys A-D. */
static inline char keypad_label(unsigned index) {
    return index < 16 ? "123456789*0#ABCD"[index] : '?';
}
/* Convert the previous column-major physical button index to logical order. */
static inline unsigned keypad_logical_index(unsigned physical) {
    static const uint8_t mapping[16] = {0,3,6,9,1,4,7,10,2,5,8,11,12,13,14,15};
    return mapping[physical];
}
