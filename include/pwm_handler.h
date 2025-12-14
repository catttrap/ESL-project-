#ifndef PWM_HANDLER_H
#define PWM_HANDLER_H

#include <stdint.h>

// Режимы мигания индикатора
typedef enum
{
    PWM_INDICATOR_OFF,
    PWM_INDICATOR_BLINK_SLOW,
    PWM_INDICATOR_BLINK_FAST,
    PWM_INDICATOR_ON
} pwm_indicator_mode_t;

// Инициализация модуля ШИМ
void pwm_handler_init(const uint32_t *led_pins);

// Установка цвета RGB
void pwm_handler_set_rgb(uint16_t r, uint16_t g, uint16_t b);

// Установка режима индикатора
void pwm_handler_set_indicator_mode(pwm_indicator_mode_t mode);

#endif