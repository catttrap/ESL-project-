#include "app_logic.h"
#include "pwm_handler.h"
#include "app_timer.h"
#include "nrf_log.h"
#include "nrfx_nvmc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/** @defgroup app_logic_timing Тайминги логики приложения
 * @{
 */
#define MAIN_TIMER_INTERVAL_MS               20   /**< Интервал таймера обновления (мс) */
#define HUE_HOLD_STEP                        1    /**< Шизменения оттенка при удержании */
#define SAT_VAL_HOLD_STEP                    1    /**< Шизменения насыщенности/яркости */
/** @} */

/** @defgroup app_logic_flash Flash-память
 * @{
 */
#define FLASH_SAVE_ADDR             0x7F000     /**< Адрес сохранения данных во Flash */
/** @} */

/**
 * @brief Режимы ввода устройства
 */
typedef enum
{
    MODE_NO_INPUT = 0,   /**< Режим без ввода */
    MODE_HUE,            /**< Режим настройки оттенка */
    MODE_SATURATION,     /**< Режим настройки насыщенности */
    MODE_VALUE,          /**< Режим настройки яркости */
    INPUT_MODE_COUNT     /**< Количество режимов */
} input_mode_t;

/**
 * @brief Структура данных для сохранения во Flash
 */
typedef struct
{
    app_logic_hsv_t current_color;                  /**< Текущий цвет */
    uint32_t count;                                 /**< Количество сохраненных цветов */
    saved_color_entry_t list[MAX_SAVED_COLORS];     /**< Массив сохраненных цветов */
} app_flash_data_t;

/** @defgroup app_logic_variables Внутренние переменные
 * @{
 */
static app_flash_data_t m_app_data;           /**< Данные приложения */
static input_mode_t     m_current_mode = MODE_NO_INPUT; /**< Текущий режим */
static bool             m_button_hold = false;          /**< Флаг удержания кнопки */
static int8_t       m_saturation_direction = -1;  /**< Направление изменения насыщенности */
static int8_t       m_value_direction = -1;       /**< Направление изменения яркости */
/** @} */

APP_TIMER_DEF(m_update_timer);  /**< Таймер обновления значений */

/**
 * @brief Сохранение всех данных во Flash-память
 */
static void save_all_data_to_flash(void)
{
    NRF_LOG_INFO("Сохранение данных во Flash по адресу 0x%08X", FLASH_SAVE_ADDR);
    
    nrfx_nvmc_page_erase(FLASH_SAVE_ADDR);
    nrfx_nvmc_words_write(FLASH_SAVE_ADDR, (uint32_t *)&m_app_data, sizeof(m_app_data) / 4);
    while (nrfx_nvmc_write_done_check() == false);
    
    NRF_LOG_INFO("Данные сохранены: текущий цвет H:%d S:%d V:%d, сохранено %d цветов", 
                  m_app_data.current_color.h, m_app_data.current_color.s, 
                  m_app_data.current_color.v, m_app_data.count);
}

/**
 * @brief Конвертация RGB в HSV
 * @param r Компонент красного (0-1000)
 * @param g Компонент зеленого (0-1000)
 * @param b Компонент синего (0-1000)
 * @param hsv Указатель на структуру для результата HSV
 */
static void convert_rgb_to_hsv(uint16_t r, uint16_t g, uint16_t b, app_logic_hsv_t *hsv)
{
    float R = r / 1000.0f;
    float G = g / 1000.0f;
    float B = b / 1000.0f;

    float cmax = MAX(R, MAX(G, B));
    float cmin = MIN(R, MIN(G, B));
    float delta = cmax - cmin;

    if (delta == 0) {
        hsv->h = 0;
    } else if (cmax == R) {
        float mod_val = fmodf(((G - B) / delta), 6);
        if (mod_val < 0)
            mod_val += 6.0f;
        hsv->h = (uint16_t)(60 * mod_val);
    } else if (cmax == G) {
        hsv->h = (uint16_t)(60 * (((B - R) / delta) + 2));
    } else {
        hsv->h = (uint16_t)(60 * (((R - G) / delta) + 4));
    }

    if (cmax == 0) {
        hsv->s = 0;
    } else {
        hsv->s = (uint8_t)((delta / cmax) * 100);
    }

    hsv->v = (uint8_t)(cmax * 100);
}

/**
 * @brief Конвертация HSV в RGB
 * @param hsv Структура цвета в HSV
 * @param r Указатель на компонент красного (0-1000)
 * @param g Указатель на компонент зеленого (0-1000)
 * @param b Указатель на компонент синего (0-1000)
 */
static void convert_hsv_to_rgb(app_logic_hsv_t hsv, uint16_t *r, uint16_t *g, uint16_t *b)
{
    float H = hsv.h;
    float S = hsv.s / 100.0f;
    float V = hsv.v / 100.0f;
    
    float C = V * S;
    float X = C * (1 - fabsf(fmodf(H / 60.0f, 2) - 1));
    float m = V - C;
    
    float R_temp, G_temp, B_temp;
    
    if (H >= 0 && H < 60) {
        R_temp = C; G_temp = X; B_temp = 0;
    } else if (H >= 60 && H < 120) {
        R_temp = X; G_temp = C; B_temp = 0;
    } else if (H >= 120 && H < 180) {
        R_temp = 0; G_temp = C; B_temp = X;
    } else if (H >= 180 && H < 240) {
        R_temp = 0; G_temp = X; B_temp = C;
    } else if (H >= 240 && H < 300) {
        R_temp = X; G_temp = 0; B_temp = C;
    } else {
        R_temp = C; G_temp = 0; B_temp = X;
    }
    
    *r = (uint16_t)((R_temp + m) * 1000);
    *g = (uint16_t)((G_temp + m) * 1000);
    *b = (uint16_t)((B_temp + m) * 1000);
}

/**
 * @brief Обновление светодиодов
 */
static void update_leds(void)
{
    uint16_t r, g, b;
    convert_hsv_to_rgb(m_app_data.current_color, &r, &g, &b);
    
    NRF_LOG_INFO("Обновление LED: HSV(H:%d S:%d V:%d)", 
                  m_app_data.current_color.h, m_app_data.current_color.s, 
                  m_app_data.current_color.v);
    
    pwm_handler_set_rgb(r, g, b);
}

/**
 * @brief Установка режима работы
 * @param new_mode Новый режим
 */
static void set_input_mode(input_mode_t new_mode)
{    
    if (new_mode == MODE_NO_INPUT && m_current_mode != MODE_NO_INPUT) {
        NRF_LOG_INFO("Выход из режима редактирования, сохранение данных");
        save_all_data_to_flash();
    }

    m_current_mode = new_mode;
    
    switch (m_current_mode)
    {
        case MODE_NO_INPUT:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_OFF);
            NRF_LOG_INFO("Режим: без ввода");
            break;
        case MODE_HUE:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_BLINK_SLOW);
            NRF_LOG_INFO("Режим: настройка оттенка");
            break;
        case MODE_SATURATION:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_BLINK_FAST);
            NRF_LOG_INFO("Режим: настройка насыщенности");
            break;
        case MODE_VALUE:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_ON);
            NRF_LOG_INFO("Режим: настройка яркости");
            break;
        default: break;
    }
}

/**
 * @brief Обработчик таймера изменения значений
 * @param p_context Контекст (не используется)
 */
static void update_timer_handler(void * p_context)
{
    if (!m_button_hold || m_current_mode == MODE_NO_INPUT) return;

    switch (m_current_mode)
    {
        case MODE_HUE:
            // Изменение оттенка (0-360)
            m_app_data.current_color.h = (m_app_data.current_color.h + HUE_HOLD_STEP) % 360;
            break;

        case MODE_SATURATION:
            // Логика маятника для насыщенности
            {
                int16_t new_sat = m_app_data.current_color.s + (m_saturation_direction * SAT_VAL_HOLD_STEP);
                
                if (new_sat >= 100)
                {
                    new_sat = 100;
                    m_saturation_direction = -1; // Разворачиваем вниз
                }
                else if (new_sat <= 0)
                {
                    new_sat = 0;
                    m_saturation_direction = 1;  // Разворачиваем вверх
                }
                m_app_data.current_color.s = (uint8_t)new_sat;
            }
            break;

        case MODE_VALUE:
            // Логика маятника для яркости
            {
                int16_t new_val = m_app_data.current_color.v + (m_value_direction * SAT_VAL_HOLD_STEP);
                
                if (new_val >= 100)
                {
                    new_val = 100;
                    m_value_direction = -1; // Разворачиваем вниз
                }
                else if (new_val <= 0)
                {
                    new_val = 0;
                    m_value_direction = 1;  // Разворачиваем вверх
                }
                m_app_data.current_color.v = (uint8_t)new_val;
            }
            break;

        default: break;
    }
    update_leds();
}

/**
 * @brief Обработка событий кнопки
 * @param event Событие кнопки
 */
void app_logic_on_button_event(button_event_t event)
{
    NRF_LOG_INFO("Событие кнопки: %d", event);
    
    switch (event)
    {
        case BUTTON_EVENT_DOUBLE_CLICK:
            NRF_LOG_INFO("Двойной клик - смена режима");
            set_input_mode((m_current_mode + 1) % INPUT_MODE_COUNT);
            break;

        case BUTTON_EVENT_PRESSED:
            NRF_LOG_INFO("Кнопка нажата");
            m_button_hold = true;
            if (m_current_mode != MODE_NO_INPUT) {
                app_timer_start(m_update_timer, APP_TIMER_TICKS(MAIN_TIMER_INTERVAL_MS), NULL);
                NRF_LOG_INFO("Запуск таймера обновления");
            }
            break;

        case BUTTON_EVENT_RELEASED:
            NRF_LOG_INFO("Кнопка отпущена");
            m_button_hold = false;
            app_timer_stop(m_update_timer);
            NRF_LOG_INFO("Остановка таймера обновления");
            break;
    }
}

/**
 * @brief Инициализация логики приложения
 * @param id_digits Указатель на массив цифр ID устройства
 */
void app_logic_init(const int *id_digits)
{
    NRF_LOG_INFO("Инициализация логики приложения");
    
    app_flash_data_t * p_flash = (app_flash_data_t *)FLASH_SAVE_ADDR;

    if (p_flash->count > MAX_SAVED_COLORS)
    {
        NRF_LOG_INFO("Данные Flash повреждены или отсутствуют, инициализация по умолчанию");
        memset(&m_app_data, 0, sizeof(app_flash_data_t));
        
        int last_two_digits = id_digits[2] * 10 + id_digits[3];
        m_app_data.current_color.h = (uint16_t)(360 * (last_two_digits / 100.0f));
        m_app_data.current_color.s = 100;
        m_app_data.current_color.v = 100;
        m_app_data.count = 0;
        
        NRF_LOG_INFO("Установлен начальный цвет по ID %d: H=%d", last_two_digits, m_app_data.current_color.h);
        save_all_data_to_flash();
    }
    else
    {
        NRF_LOG_INFO("Загрузка данных из Flash");
        memcpy(&m_app_data, p_flash, sizeof(app_flash_data_t));
        
        // Корректировка значений при необходимости
        if (m_app_data.current_color.h > 360) m_app_data.current_color.h = 0;
        if (m_app_data.current_color.s > 100) m_app_data.current_color.s = 100;
        if (m_app_data.current_color.v > 100) m_app_data.current_color.v = 100;
        
        NRF_LOG_INFO("Загружен цвет: H=%d S=%d V=%d, сохранено %d цветов", 
                      m_app_data.current_color.h, m_app_data.current_color.s, 
                      m_app_data.current_color.v, m_app_data.count);
    }

    m_saturation_direction = -1;
    m_value_direction = -1;

    app_timer_create(&m_update_timer, APP_TIMER_MODE_REPEATED, update_timer_handler);
    NRF_LOG_INFO("Таймер обновления создан");

    set_input_mode(MODE_NO_INPUT);
    update_leds();
    
    NRF_LOG_INFO("Логика приложения инициализирована");
}

/**
 * @brief Установка цвета в формате HSV
 * @param h Оттенок (0-360)
 * @param s Насыщенность (0-100)
 * @param v Яркость (0-100)
 */
void app_logic_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    NRF_LOG_INFO("Установка цвета HSV: H=%d S=%d V=%d", h, s, v);
    
    m_app_data.current_color.h = (h > 360) ? 360 : h;
    m_app_data.current_color.s = (s > 100) ? 100 : s;
    m_app_data.current_color.v = (v > 100) ? 100 : v;

    set_input_mode(MODE_NO_INPUT);
    update_leds();
    save_all_data_to_flash();
}

/**
 * @brief Установка цвета в формате RGB
 * @param r Компонент красного (0-1000)
 * @param g Компонент зеленого (0-1000)
 * @param b Компонент синего (0-1000)
 */
void app_logic_set_rgb(uint16_t r, uint16_t g, uint16_t b)
{
   
    if (r > 1000) r = 1000;
    if (g > 1000) g = 1000;
    if (b > 1000) b = 1000;

    set_input_mode(MODE_NO_INPUT); 
    convert_rgb_to_hsv(r, g, b, &m_app_data.current_color);
    update_leds();
    save_all_data_to_flash();
}

/**
 * @brief Сохранение HSV цвета в список
 * @param h Оттенок (0-360)
 * @param s Насыщенность (0-100)
 * @param v Яркость (0-100)
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_color_hsv(uint16_t h, uint8_t s, uint8_t v, const char * name)
{    
    if (m_app_data.count >= MAX_SAVED_COLORS)
    {
        return false;
    }

    for (uint32_t i = 0; i < m_app_data.count; i++)
    {
        if (strcmp(m_app_data.list[i].name, name) == 0)
        {
            return false;
        }
    }
    
    uint32_t id = m_app_data.count;
    
    strncpy(m_app_data.list[id].name, name, COLOR_NAME_LEN - 1);
    m_app_data.list[id].name[COLOR_NAME_LEN - 1] = '\0';

    m_app_data.list[id].color.h = h;
    m_app_data.list[id].color.s = s;
    m_app_data.list[id].color.v = v;

    m_app_data.count++;
        
    save_all_data_to_flash();
    
    return true;
}

/**
 * @brief Сохранение RGB цвета в список
 * @param r Компонент красного (0-1000)
 * @param g Компонент зеленого (0-1000)
 * @param b Компонент синего (0-1000)
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_color_rgb(uint16_t r, uint16_t g, uint16_t b, const char * name)
{
   
    app_logic_hsv_t temp_hsv;
    convert_rgb_to_hsv(r, g, b, &temp_hsv);
      
    return app_logic_save_color_hsv(temp_hsv.h, temp_hsv.s, temp_hsv.v, name);
}

/**
 * @brief Сохранение текущего активного цвета в список
 * @param name Имя цвета
 * @return true - цвет сохранен успешно, false - ошибка
 */
bool app_logic_save_current_color(const char * name)
{
  
    return app_logic_save_color_hsv(m_app_data.current_color.h, 
                                    m_app_data.current_color.s, 
                                    m_app_data.current_color.v, 
                                    name);
}

/**
 * @brief Удаление цвета по имени
 * @param name Имя цвета для удаления
 * @return true - цвет удален, false - цвет не найден
 */
bool app_logic_del_color(const char * name)
{
   
    for (uint32_t i = 0; i < m_app_data.count; i++)
    {
        if (strcmp(m_app_data.list[i].name, name) == 0)
        {
           
            // Сдвигаем массив
            for (uint32_t j = i; j < m_app_data.count - 1; j++)
            {
                m_app_data.list[j] = m_app_data.list[j+1];
            }
            m_app_data.count--;
            
           
            save_all_data_to_flash();
            return true;
        }
    }

    return false;
}

/**
 * @brief Применение цвета из списка
 * @param name Имя цвета для применения
 * @return true - цвет применен, false - цвет не найден
 */
bool app_logic_apply_color(const char * name)
{    
    for (uint32_t i = 0; i < m_app_data.count; i++)
    {
        if (strcmp(m_app_data.list[i].name, name) == 0)
        {
           
            app_logic_set_hsv(m_app_data.list[i].color.h,
                              m_app_data.list[i].color.s,
                              m_app_data.list[i].color.v);
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Получение списка сохраненных цветов
 * @param count Указатель на переменную для получения количества цветов
 * @return Указатель на массив сохраненных цветов
 */
const saved_color_entry_t * app_logic_get_list(uint8_t * count)
{
    *count = (uint8_t)m_app_data.count;
    return m_app_data.list;
}