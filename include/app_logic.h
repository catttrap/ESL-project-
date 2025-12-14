#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>
#include "button_handler.h"

// Инициализация логики приложения
void app_logic_init(const int *id_digits);

// Обработчик событий от кнопки (вызывается из button_handler)
void app_logic_on_button_event(button_event_t event);

// Установка цвета в формате RGB
void app_logic_set_rgb(uint16_t r, uint16_t g, uint16_t b);

// Установка цвета в формате HSV
void app_logic_set_hsv(uint16_t h, uint8_t s, uint8_t v);

#endif