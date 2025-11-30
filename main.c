#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "nrf_gpio.h"
#include "nrfx_pwm.h"
#include "nrfx_gpiote.h"
#include "app_timer.h"
#include "nrfx_clock.h"
#include "nrfx_nvmc.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_log_backend_usb.h"

/* ---------------- Pins ---------------- */
#define INDICATOR_LED_PIN NRF_GPIO_PIN_MAP(0,6)
#define LED_RED     NRF_GPIO_PIN_MAP(0,8)    /**< Пин красного светодиода */
#define LED_GREEN   NRF_GPIO_PIN_MAP(1,9)    /**< Пин зеленого светодиода */
#define LED_BLUE    NRF_GPIO_PIN_MAP(0,12)   /**< Пин синего светодиода */
#define BUTTON_PIN  NRF_GPIO_PIN_MAP(1,6)    /**< Пин кнопки (активный низкий уровень) */

/* ---------------- PWM ---------------- */
#define PWM_CHANNELS           4    /**< Количество каналов PWM */
#define DUTY_MAX          1000  /**< Максимальное значение скважности (100%) для ШИМ 1кГц */

/* ---------------- Timings ---------------- */
#define MAIN_TIMER_INTERVAL_MS 20   /**< Интервал основного таймера в мс */
#define DEBOUNCE_MS       200   /**< Время антидребезга в миллисекундах */
#define DOUBLE_CLICK_MS   500   /**< Таймаут для обнаружения двойного клика */

#define HOLD_INTERVAL_MS       MAIN_TIMER_INTERVAL_MS   /**< Интервал изменения при удержании кнопки */
#define HUE_HOLD_STEP          1    /**< Шаг изменения оттенка при удержании */
#define SAT_VAL_HOLD_STEP      1    /**< Шаг изменения насыщенности и яркости при удержании */

#define SLOW_BLINK_PERIOD_MS   1500 /**< Период медленного мигания в мс */  
#define FAST_BLINK_PERIOD_MS   500  /**< Период быстрого мигания в мс */

/* ---------------- Flash Memory ---------------- */
#define FLASH_SAVE_ADDR 0x0007F000 /**< Адрес для сохранения данных в Flash (последняя страница) */


/* ---------------- Forward decl ---------------- */
void pwm_init(void);
void button_init(void);
void main_timer_handler(void * p_context);
void debounce_timer_handler(void * p_context);
void double_click_timer_handler(void * p_context);
void button_press_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
static void update_indicator_for_current_mode(void);
static inline int clamp_value(int value, int min, int max);
static void convert_hsv_to_rgb(float hue, int saturation, int value, uint16_t *red, uint16_t *green, uint16_t *blue);
static void update_pwm_outputs(uint16_t indicator, uint16_t red, uint16_t green, uint16_t blue);


static nrfx_pwm_t m_pwm_instance = NRFX_PWM_INSTANCE(0);    /**< Экземпляр PWM */
static nrf_pwm_values_individual_t m_pwm_channel_values;    /**< Значения для каналов PWM */



/**
 * @brief Режимы ввода устройства
 */
typedef enum {
    MODE_NO_INPUT = 0,  /**< Режим без ввода */
    MODE_HUE,           /**< Режим изменения оттенка */
    MODE_SATURATION,    /**< Режим изменения насыщенности */
    MODE_VALUE          /**< Режим изменения яркости */
} input_mode_t;


static volatile input_mode_t m_current_mode = MODE_NO_INPUT;    /**< Текущий режим работы */

static float m_current_hue = 0.0f;  /**< Текущий оттенок (0-360 градусов) */    
static int   m_current_saturation = 100;    /**< Текущая насыщенность (0-100%) */     
static int   m_current_value = 100; /**< Текущая яркость (0-100%) */    

static int m_hue_direction = 1; /**< Направление изменения оттенка */
static int m_saturation_direction = 1;  /**< Направление изменения насыщенности */
static int m_value_direction = 1;   /**< Направление изменения яркости */

static int m_indicator_brightness = 0;  /**< Текущая яркость индикатора */
static int m_indicator_direction = 1;   /**< Направление изменения яркости индикатора */


static volatile bool m_button_blocked = false;  /**< Флаг блокировки кнопки (антидребезг) */
static volatile bool m_first_click_detected = false;    /**< Флаг обнаружения первого клика */
static volatile bool m_button_hold = false; /**< Флаг удержания кнопки */

APP_TIMER_DEF(main_timer);  /**< Таймер основного цикла */
APP_TIMER_DEF(debounce_timer);  /**< Таймер антидребезга */
APP_TIMER_DEF(double_click_timer);  /**< Таймер двойного клика */


static uint32_t m_indicator_step = 1;   /**< Шаг изменения индикатора */

static uint32_t m_indicator_period_ms = SLOW_BLINK_PERIOD_MS;   /**< Период мигания индикатора */

// Конвертирование HSV параметров в 32-битное число
static uint32_t conv_hsv_params_to_uint32(float hue, int saturation, int value)
{
    // Преобразуем hue в целое число с фиксированной точностью (0.1 градус точность)
    uint16_t hue_int = (uint16_t)(hue * 10.0f);
    uint8_t sat_int = (uint8_t)saturation;
    uint8_t val_int = (uint8_t)value;
    
    return ((uint32_t)hue_int << 16) | ((uint32_t)sat_int << 8) | val_int;
}

// Конвертирование 32-битного числа в HSV параметры
static void conv_uint32_to_hsv_params(uint32_t packed, float *hue, int *saturation, int *value)
{
    uint16_t hue_int = (uint16_t)((packed >> 16) & 0xFFFF);
    uint8_t sat_int = (uint8_t)((packed >> 8) & 0xFF);
    uint8_t val_int = (uint8_t)(packed & 0xFF);
    
    *hue = hue_int / 10.0f;
    *saturation = (int)sat_int;
    *value = (int)val_int;
}

// Сохранение текущих настроек в память
static void save_hsv_to_flash(void)
{
    uint32_t data_to_write = conv_hsv_params_to_uint32(m_current_hue, m_current_saturation, m_current_value);
    
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

    NRF_LOG_INFO("Saving HSV to flash: H=%.1f S=%d V=%d",
             m_current_hue, m_current_saturation, m_current_value);
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
    float loaded_hue;
    int loaded_saturation, loaded_value;
    conv_uint32_to_hsv_params(data, &loaded_hue, &loaded_saturation, &loaded_value);
    NRF_LOG_INFO("Loaded from flash: H=%.1f S=%d V=%d",
             loaded_hue, loaded_saturation, loaded_value);

    // Проверяем валидность данных
    if (loaded_hue >= 0.0f && loaded_hue <= 360.0f &&
        loaded_saturation >= 0 && loaded_saturation <= 100 &&
        loaded_value >= 0 && loaded_value <= 100) {
        
        m_current_hue = loaded_hue;
        m_current_saturation = loaded_saturation;
        m_current_value = loaded_value;
        return true;
    }
    NRF_LOG_WARNING("Flash data invalid, using defaults");
    return false;
}

/**
* @brief Вспомогательная функция: ограничение целого значения в диапазоне.
* @param v значение
* @param min нижняя граница
* @param max верхняя граница
* @return ограниченное значение
*/
static inline int clamp_value(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief Конвертирует цвет из HSV в RGB пространство
 * @param hue Оттенок (0-360 градусов)
 * @param saturation Насыщенность (0-100%)
 * @param value Яркость (0-100%)
 * @param red Указатель для красной компоненты
 * @param green Указатель для зеленой компоненты
 * @param blue Указатель для синей компоненты
 */
static void convert_hsv_to_rgb(float hue, int saturation, int value, uint16_t *red, uint16_t *green, uint16_t *blue) {
    float hue_normalized = hue;
    float saturation_normalized = saturation / 100.0f;
    float value_normalized = value / 100.0f;

    // Если насыщенность нулевая - оттенки серого
    if (saturation_normalized <= 0.0f) {
        uint16_t gray_value = (uint16_t)(value_normalized * DUTY_MAX + 0.5f);
        *red = *green = *blue = gray_value;
        return;
    }

    // Нормализация оттенка
    if (hue_normalized >= 360.0f) hue_normalized = 0.0f;
    float hue_sector = hue_normalized / 60.0f;
    int sector_index = (int)floorf(hue_sector);
    float fractional = hue_sector - sector_index;
    
    // Промежуточные значения
    float p = value_normalized * (1.0f - saturation_normalized);
    float q = value_normalized * (1.0f - saturation_normalized * fractional);
    float t = value_normalized * (1.0f - saturation_normalized * (1.0f - fractional));

    float red_float = 0, green_float = 0, blue_float = 0;
    
    // Выбор сектора цветового круга
    switch (sector_index) {
        case 0: red_float = value_normalized; green_float = t; blue_float = p; break;
        case 1: red_float = q; green_float = value_normalized; blue_float = p; break;
        case 2: red_float = p; green_float = value_normalized; blue_float = t; break;
        case 3: red_float = p; green_float = q; blue_float = value_normalized; break;
        case 4: red_float = t; green_float = p; blue_float = value_normalized; break;
        default: red_float = value_normalized; green_float = p; blue_float = q; break;
    }

    // Конвертация в PWM значения
    *red = (uint16_t)clamp_value((int)roundf(red_float * DUTY_MAX), 0, DUTY_MAX);
    *green = (uint16_t)clamp_value((int)roundf(green_float * DUTY_MAX), 0, DUTY_MAX);
    *blue = (uint16_t)clamp_value((int)roundf(blue_float * DUTY_MAX), 0, DUTY_MAX);
}

/**
 * @brief Обновляет выходы PWM
 * @param indicator Яркость индикаторного светодиода
 * @param red Яркость красного канала
 * @param green Яркость зеленого канала
 * @param blue Яркость синего канала
 */
static void update_pwm_outputs(uint16_t indicator, uint16_t red, uint16_t green, uint16_t blue) {
    m_pwm_channel_values.channel_0 = indicator;
    m_pwm_channel_values.channel_1 = red;
    m_pwm_channel_values.channel_2 = green;
    m_pwm_channel_values.channel_3 = blue;

    nrf_pwm_sequence_t sequence = {
        .values.p_individual = &m_pwm_channel_values,
        .length = PWM_CHANNELS,
        .repeats = 0,
        .end_delay = 0
    };

    nrfx_pwm_simple_playback(&m_pwm_instance, &sequence, 1, 0);
    
    NRF_LOG_DEBUG("PWM R=%d G=%d B=%d IND=%d", red, green, blue, indicator);

}

/**
 * @brief Обновляет параметры индикатора для текущего режима
 */
static void update_indicator_for_current_mode(void) {
    switch (m_current_mode) {
        case MODE_NO_INPUT:
            m_indicator_period_ms = 0; // Индикатор выключен
            break;
        case MODE_HUE:
            m_indicator_period_ms = SLOW_BLINK_PERIOD_MS; // Медленное мигание
            break;
        case MODE_SATURATION:
            m_indicator_period_ms = FAST_BLINK_PERIOD_MS; // Быстрое мигание
            break;
        case MODE_VALUE:
            m_indicator_period_ms = 1; // Постоянно включен
            break;
    }
    
    // Расчет шага изменения индикатора
    if (m_indicator_period_ms > 0) {
        // Половина периода на нарастание яркости
        m_indicator_step = (int)ceilf((float)DUTY_MAX * 
                         ((float)MAIN_TIMER_INTERVAL_MS / (m_indicator_period_ms / 2.0f)));
    } else {
        m_indicator_step = DUTY_MAX;
    }
    
    if (m_indicator_step < 1) m_indicator_step = 1;
}

/**
 * @brief Инициализация PWM
 */
void pwm_init(void) {
    nrfx_pwm_config_t pwm_config = NRFX_PWM_DEFAULT_CONFIG;
    pwm_config.output_pins[0] = INDICATOR_LED_PIN;   // LED1 - индикатор
    pwm_config.output_pins[1] = LED_RED;     // LED2 - красный
    pwm_config.output_pins[2] = LED_GREEN;   // LED2 - зеленый
    pwm_config.output_pins[3] = LED_BLUE;    // LED2 - синий
    pwm_config.base_clock = NRF_PWM_CLK_1MHz;
    pwm_config.count_mode = NRF_PWM_MODE_UP;
    pwm_config.top_value  = DUTY_MAX;
    pwm_config.load_mode  = NRF_PWM_LOAD_INDIVIDUAL;
    pwm_config.step_mode  = NRF_PWM_STEP_AUTO;

    nrfx_pwm_init(&m_pwm_instance, &pwm_config, NULL);

    // Инициализация значений каналов
    m_pwm_channel_values.channel_0 = 0;
    m_pwm_channel_values.channel_1 = 0;
    m_pwm_channel_values.channel_2 = 0;
    m_pwm_channel_values.channel_3 = 0;

    // Создание и запуск основного таймера
    app_timer_create(&main_timer, APP_TIMER_MODE_REPEATED, main_timer_handler);
    app_timer_start(main_timer, APP_TIMER_TICKS(MAIN_TIMER_INTERVAL_MS), NULL);
}

/**
 * @brief Инициализация кнопки
 */
void button_init(void) {
    if (!nrfx_gpiote_is_init()) {
        nrfx_gpiote_init();
    }

    // Настройка пина кнопки
    nrf_gpio_cfg_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP);

    // Конфигурация GPIOTE для кнопки
    nrfx_gpiote_in_config_t input_config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    input_config.pull = NRF_GPIO_PIN_PULLUP;

    nrfx_gpiote_in_init(BUTTON_PIN, &input_config, button_press_handler);
    nrfx_gpiote_in_event_enable(BUTTON_PIN, true);

    // Создание таймеров для обработки кнопки
    app_timer_create(&debounce_timer, APP_TIMER_MODE_SINGLE_SHOT, debounce_timer_handler);
    app_timer_create(&double_click_timer, APP_TIMER_MODE_SINGLE_SHOT, double_click_timer_handler);
}

/**
 * @brief Обработчик таймера антидребезга
 */
void debounce_timer_handler(void *p_context) {
    (void)p_context;
    m_button_blocked = false;
}

/**
 * @brief Обработчик таймера двойного клика
 */
void double_click_timer_handler(void *p_context) {
    (void)p_context;
    m_first_click_detected = false;
}

/**
 * @brief Обработчик нажатия кнопки
 */
void button_press_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    (void)pin; 
    (void)action;

    // Пропуск если кнопка заблокирована (антидребезг)
    if (m_button_blocked) return;

    m_button_blocked = true;
    app_timer_start(debounce_timer, APP_TIMER_TICKS(DEBOUNCE_MS), NULL);

    // Обработка одиночного/двойного клика
    if (!m_first_click_detected) {
        m_first_click_detected = true;
        app_timer_start(double_click_timer, APP_TIMER_TICKS(DOUBLE_CLICK_MS), NULL);
    } else {
        m_first_click_detected = false;
        app_timer_stop(double_click_timer);

        // Циклическое переключение режимов
         m_current_mode = (m_current_mode + 1) % 4;
         NRF_LOG_INFO("Mode switched to %d", m_current_mode);

        // Сброс направлений изменения
        m_hue_direction = 1;
        m_saturation_direction = 1;
        m_value_direction = 1;

        update_indicator_for_current_mode();
    }

    m_button_hold = true;
}

/**
 * @brief Обработчик основного таймера
 */
void main_timer_handler(void *p_context) {
    (void)p_context;

    static bool needs_save = false;
    static float last_hue = 0.0f;
    static int last_saturation = 100;
    static int last_value = 100;

    // Проверка отпускания кнопки
    if (m_button_hold) {
        if (nrf_gpio_pin_read(BUTTON_PIN) != 0) {
            m_button_hold = false;
        }
    }

    // Обработка удержания кнопки в активных режимах
    if (m_button_hold && m_current_mode != MODE_NO_INPUT) {
        switch (m_current_mode) {
            case MODE_HUE:
                m_current_hue += m_hue_direction * HUE_HOLD_STEP;
                if (m_current_hue >= 360.0f) {
                    m_current_hue = 360.0f;
                    m_hue_direction = -1;
                } else if (m_current_hue <= 0.0f) {
                    m_current_hue = 0.0f;
                    m_hue_direction = 1;
                }
                needs_save = true;
                NRF_LOG_INFO("Hue change: %d -> %d", (int)(m_current_hue - m_hue_direction), (int)m_current_hue);
                break;
                
            case MODE_SATURATION:
                m_current_saturation += m_saturation_direction * SAT_VAL_HOLD_STEP;
                if (m_current_saturation >= 100) {
                    m_current_saturation = 100;
                    m_saturation_direction = -1;
                } else if (m_current_saturation <= 0) {
                    m_current_saturation = 0;
                    m_saturation_direction = 1;
                }
                needs_save = true;
                NRF_LOG_INFO("Saturation: %d", m_current_saturation);
                break;
                
            case MODE_VALUE:
                m_current_value += m_value_direction * SAT_VAL_HOLD_STEP;
                if (m_current_value >= 100) {
                    m_current_value = 100;
                    m_value_direction = -1;
                } else if (m_current_value <= 0) {
                    m_current_value = 0;
                    m_value_direction = 1;
                }
                needs_save = true;
                NRF_LOG_INFO("Value: %d", m_current_value);
                break;
                
            default:
                break;
        }
    }

     // Сохраняем в Flash при изменении параметров
    if (needs_save && !m_button_hold) {
        if (last_hue != m_current_hue || 
            last_saturation != m_current_saturation || 
            last_value != m_current_value) {
            
            save_hsv_to_flash();
            last_hue = m_current_hue;
            last_saturation = m_current_saturation;
            last_value = m_current_value;
        }
        needs_save = false;
    }

    // Обновление индикатора
    uint16_t indicator_brightness = 0;
    if (m_current_mode == MODE_NO_INPUT) {
        indicator_brightness = 0;
        m_indicator_brightness = 0;
    } else if (m_current_mode == MODE_VALUE) {
        indicator_brightness = DUTY_MAX;
        m_indicator_brightness = DUTY_MAX;
    } else {
        if (m_indicator_period_ms > 0) {
            m_indicator_brightness += (int)m_indicator_step * m_indicator_direction;
            if (m_indicator_brightness >= (int)DUTY_MAX) {
                m_indicator_brightness = DUTY_MAX;
                m_indicator_direction = -1;
            } else if (m_indicator_brightness <= 0) {
                m_indicator_brightness = 0;
                m_indicator_direction = 1;
            }
            indicator_brightness = (uint16_t)clamp_value(m_indicator_brightness, 0, DUTY_MAX);
        } else {
            indicator_brightness = 0;
        }
    }

    // Обновление цвета RGB светодиода
    uint16_t red, green, blue;
    convert_hsv_to_rgb(m_current_hue, m_current_saturation, m_current_value, &red, &green, &blue);
    update_pwm_outputs(indicator_brightness, red, green, blue);
}

/**
 * @brief Основная функция программы
 */
int main(void) {
    // Инициализация тактирования
    nrfx_clock_init(NULL);
    nrfx_clock_lfclk_start();
    while(!nrfx_clock_lfclk_is_running());

    // Инициализация таймеров
    app_timer_init();

    // Инициализация логирования
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    
    bool loaded = load_hsv_from_flash();
    
    if (!loaded) {
        // Установка начальных значений HSV 
        m_current_saturation = 100;
        m_current_value = 100;
        m_current_hue = (1.0f / 100.0f) * 360.0f; // 1% от 360° = 3.6°
    }

    // Настройка индикатора для текущего режима
    update_indicator_for_current_mode();

    // Инициализация периферии
    pwm_init();
    button_init();

    // Установка начального цвета
    uint16_t red, green, blue;
    convert_hsv_to_rgb(m_current_hue, m_current_saturation, m_current_value, &red, &green, &blue);
    update_pwm_outputs(0, red, green, blue);

    // Основной цикл
    while (1) {
        NRF_LOG_PROCESS();
        __WFE();
    }
}
