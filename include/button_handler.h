#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>

// Возможные события кнопки
typedef enum
{
    BUTTON_EVENT_PRESSED,       // Нажатие (начало удержания)
    BUTTON_EVENT_RELEASED,      // Отпускание
    BUTTON_EVENT_DOUBLE_CLICK   // Двойное нажатие
} button_event_t;

// Инициализация обработчика кнопки
void button_handler_init(uint32_t button_pin);

#endif