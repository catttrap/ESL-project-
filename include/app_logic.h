#ifndef APP_LOGIC_H
#define APP_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "button_handler.h"

/**
 * @defgroup app_logic Конфигурация логики приложения
 * @{
 */
#define MAX_SAVED_COLORS 10  /**< Максимальное количество сохраненных цветов */
#define COLOR_NAME_LEN   31  /**< Длина имени цвета */
/** @} */

/**
 * @brief Структура цвета в формате HSV
 */
typedef struct
{
    uint16_t h; /**< Оттенок: 0-360 градусов */
    uint8_t  s; /**< Насыщенность: 0-100% */
    uint8_t  v; /**< Яркость: 0-100% */
} app_logic_hsv_t;

/**
 * @brief Структура записи сохраненного цвета
 */
typedef struct
{
    char name[COLOR_NAME_LEN];  /**< Имя цвета */
    app_logic_hsv_t color;       /**< Значения цвета в HSV */
} saved_color_entry_t;

/**
 * @brief Инициализация логики приложения
 * @param id_digits Указатель на массив цифр ID устройства
 */
void app_logic_init(const int *id_digits);

/**
 * @brief Обработчик событий от кнопки
 * @param event Событие кнопки
 * 
 * Вызывается из button_handler при нажатиях на кнопку
 */
void app_logic_on_button_event(button_event_t event);

/**
 * @brief Установка цвета в формате RGB
 * @param r Компонент красного (0-1000)
 * @param g Компонент зеленого (0-1000)
 * @param b Компонент синего (0-1000)
 */
void app_logic_set_rgb(uint16_t r, uint16_t g, uint16_t b);

/**
 * @brief Установка цвета в формате HSV
 * @param h Оттенок (0-360)
 * @param s Насыщенность (0-100)
 * @param v Яркость (0-100)
 */
void app_logic_set_hsv(uint16_t h, uint8_t s, uint8_t v);

/**
 * @brief Сохранение HSV цвета в список
 * @param h Оттенок (0-360)
 * @param s Насыщенность (0-100)
 * @param v Яркость (0-100)
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_color_hsv(uint16_t h, uint8_t s, uint8_t v, const char * name);

/**
 * @brief Сохранение RGB цвета в список
 * @param r Компонент красного (0-1000)
 * @param g Компонент зеленого (0-1000)
 * @param b Компонент синего (0-1000)
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_color_rgb(uint16_t r, uint16_t g, uint16_t b, const char * name);

/**
 * @brief Сохранение текущего активного цвета в список
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_current_color(const char * name);

/**
 * @brief Удаление цвета по имени
 * @param name Имя цвета для удаления
 * @return true - цвет удален, false - цвет не найден
 */
bool app_logic_del_color(const char * name);

/**
 * @brief Применение цвета из списка
 * @param name Имя цвета для применения
 * @return true - цвет применен, false - цвет не найден
 */
bool app_logic_apply_color(const char * name);

/**
 * @brief Получение списка сохраненных цветов
 * @param count Указатель на переменную для получения количества цветов
 * @return Указатель на массив сохраненных цветов
 */
const saved_color_entry_t * app_logic_get_list(uint8_t * count);

#endif