#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void audio_pwm_init(uint32_t rate);
void audio_pwm_set_rate(uint32_t rate);
void audio_pwm_set_service_callback(void (*callback)(void *), void *context);
void audio_pwm_begin(uint8_t bits);
bool audio_pwm_write(const uint8_t *pcm, size_t count);
bool audio_pwm_end(void);
void audio_pwm_stop(void);
void audio_pwm_print_status(void);
