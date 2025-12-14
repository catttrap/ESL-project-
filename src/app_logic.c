#include "app_logic.h"
#include "pwm_handler.h"
#include "app_timer.h"
#include "nrf_log.h"
#include "nrfx_nvmc.h"
#include <math.h>

/* ---------------- Timings ---------------- */
#define MAIN_TIMER_INTERVAL_MS    20
#define HUE_HOLD_STEP                    1
#define SAT_VAL_HOLD_STEP                    1
#define VAL_HOLD_STEP                    1

/* ---------------- Flash Memory ---------------- */
#define FLASH_SAVE_ADDR             0x7F000

/**
 * @brief Режимы ввода устройства
 */
typedef enum
{
    MODE_NO_INPUT = 0,
    MODE_HUE,
    MODE_SATURATION,
    MODE_VALUE,
    INPUT_MODE_COUNT
} input_mode_t;

// Структура HSV
typedef struct
{
    uint16_t h; // 0-360
    uint8_t  s; // 0-100
    uint8_t  v; // 0-100
} hsv_t;

static hsv_t        m_current_hsv;
static input_mode_t m_current_mode = MODE_NO_INPUT;
static bool         m_button_hold = false;


static int8_t       m_saturation_direction = -1;
static int8_t       m_value_direction = -1;

APP_TIMER_DEF(m_update_timer);

// Упаковка HSV структуры в 32-битное число
static uint32_t pack_hsv(hsv_t hsv)
{
    return ((uint32_t)hsv.h << 16) | ((uint32_t)hsv.s << 8) | hsv.v;
}

// Распаковка 32-битного числа в структуру HSV
static hsv_t unpack_hsv(uint32_t packed)
{
    hsv_t hsv;
    hsv.h = (uint16_t)((packed >> 16));
    hsv.s = (uint8_t)((packed >> 8));
    hsv.v = (uint8_t)(packed);
    return hsv;
}

// Сохранение текущех настроек в память
static void save_hsv_to_flash(void)
{
    uint32_t data_to_write = pack_hsv(m_current_hsv);
    
    uint32_t *p_flash = (uint32_t *)FLASH_SAVE_ADDR;
    uint32_t current_flash_data = *p_flash;

    // Если данные не изменились, не перезаписываем
    if (current_flash_data == data_to_write)
        return;

    // Стираем страницу памяти
    nrfx_nvmc_page_erase(FLASH_SAVE_ADDR);
    
    // Ждем завершения стирания
    while (!nrfx_nvmc_write_done_check());
    
    // Записываем данные
    nrfx_nvmc_word_write(FLASH_SAVE_ADDR, data_to_write);
    
    // Ждем завершения записи
    while (!nrfx_nvmc_write_done_check());

}

// Чтение сохраненного значения из памяти
static bool load_hsv_from_flash(void)
{
    uint32_t *p_flash = (uint32_t *)FLASH_SAVE_ADDR;
    uint32_t data = *p_flash;

    // Проверяем, была ли страница записана ранее (0xFFFFFFFF - стертая память)
    if (data == 0xFFFFFFFF)
        return false;
    // Распаковываем данные
    m_current_hsv = unpack_hsv(data);
    // Проверяем валидность данных
    if (m_current_hsv.h > 360) m_current_hsv.h = 0;
    if (m_current_hsv.s > 100) m_current_hsv.s = 100;
    if (m_current_hsv.v > 100) m_current_hsv.v = 100;

    return true;
}

// Конвертация RGB -> HSV
static void rgb_to_hsv(uint16_t r, uint16_t g, uint16_t b, hsv_t *hsv)
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
        hsv->h = (uint16_t)(60 * fmodf(((G - B) / delta), 6));
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

// Конвертация HSV -> RGB
static void hsv_to_rgb(hsv_t hsv, uint16_t *r, uint16_t *g, uint16_t *b)
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


// Обновление LED
static void update_leds(void)
{
    uint16_t r, g, b;
    hsv_to_rgb(m_current_hsv, &r, &g, &b);
    pwm_handler_set_rgb(r, g, b);
}

// Смена режима
static void set_mode(input_mode_t new_mode)
{
    if (new_mode == MODE_NO_INPUT && m_current_mode != MODE_NO_INPUT)
        save_hsv_to_flash();

    m_current_mode = new_mode;
    
    switch (m_current_mode)
    {
        case MODE_NO_INPUT:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_OFF);
            break;
        case MODE_HUE:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_BLINK_SLOW);
            break;
        case MODE_SATURATION:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_BLINK_FAST);
            break;
        case MODE_VALUE:
            pwm_handler_set_indicator_mode(PWM_INDICATOR_ON);
            break;
        default: break;
    }
}

// Таймер изменения значений
static void update_timer_handler(void * p_context)
{
    if (!m_button_hold || m_current_mode == MODE_NO_INPUT) return;

    switch (m_current_mode)
    {
        case MODE_HUE:
            // Hue (0-360)
            m_current_hsv.h = (m_current_hsv.h + HUE_HOLD_STEP) % 360;
            
            break;

        case MODE_SATURATION:
            // Логика маятника для Saturation
            {
                int16_t new_sat = m_current_hsv.s + (m_saturation_direction * SAT_VAL_HOLD_STEP);
                
                if (new_sat >= 100)
                {
                    new_sat = 100;
                    m_saturation_direction = -1; 
                }
                else if (new_sat <= 0)
                {
                    new_sat = 0;
                    m_saturation_direction = 1;  
                }
                m_current_hsv.s = (uint8_t)new_sat;
            }
            break;

        case MODE_VALUE:
            // Логика маятника для Value (Яркость)
            {
                int16_t new_val = m_current_hsv.v + (m_value_direction * VAL_HOLD_STEP);
                
                if (new_val >= 100)
                {
                    new_val = 100;
                    m_value_direction = -1; 
                }
                else if (new_val <= 0)
                {
                    new_val = 0;
                    m_value_direction = 1;  
                }
                m_current_hsv.v = (uint8_t)new_val;
            }
            break;

        default: break;
    }
    update_leds();
}

// Обработка событий кнопки
void app_logic_on_button_event(button_event_t event)
{
    switch (event)
    {
        case BUTTON_EVENT_DOUBLE_CLICK:
            set_mode((m_current_mode + 1) % INPUT_MODE_COUNT);
            break;

        case BUTTON_EVENT_PRESSED:
            m_button_hold = true;
            if (m_current_mode != MODE_NO_INPUT) {
                app_timer_start(m_update_timer, APP_TIMER_TICKS(MAIN_TIMER_INTERVAL_MS), NULL);
            }
            break;

        case BUTTON_EVENT_RELEASED:
            m_button_hold = false;
            app_timer_stop(m_update_timer);
            break;
    }
}

// Инициализация логики
void app_logic_init(const int *id_digits)
{
    bool loaded = load_hsv_from_flash();

    if (loaded)
    {
        m_saturation_direction = -1;
        m_value_direction = -1;
    }
    else
    {
        int last_two_digits = id_digits[2] * 10 + id_digits[3];
        m_current_hsv.h = (uint16_t)(360 * (last_two_digits / 100.0f));
        m_current_hsv.s = 100;
        m_current_hsv.v = 100;
        
        m_saturation_direction = -1;
        m_value_direction = -1;
    }

    app_timer_create(&m_update_timer, APP_TIMER_MODE_REPEATED, update_timer_handler);

    set_mode(MODE_NO_INPUT);
    update_leds();
}

void app_logic_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    m_current_hsv.h = (h > 360) ? 360 : h;
    m_current_hsv.s = (s > 100) ? 100 : s;
    m_current_hsv.v = (v > 100) ? 100 : v;

    set_mode(MODE_NO_INPUT);
    update_leds();
    save_hsv_to_flash();
}

void app_logic_set_rgb(uint16_t r, uint16_t g, uint16_t b)
{
    if (r > 1000) r = 1000;
    if (g > 1000) g = 1000;
    if (b > 1000) b = 1000;

    set_mode(MODE_NO_INPUT); 
    rgb_to_hsv(r, g, b, &m_current_hsv);
    update_leds();
    save_hsv_to_flash();
}