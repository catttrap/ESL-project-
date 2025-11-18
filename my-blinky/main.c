#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrfx_pwm.h"
#include "nrfx_gpiote.h"
#include "app_timer.h"
#include "app_error.h"

/** @defgroup Конфигурация пинов
 *  @{
 */
#define LED_RED   NRF_GPIO_PIN_MAP(0,6)   /**< Пин красного светодиода */
#define LED_GREEN NRF_GPIO_PIN_MAP(0,8)   /**< Пин зеленого светодиода */
#define LED_BLUE  NRF_GPIO_PIN_MAP(0,12)  /**< Пин синего светодиода */
#define BUTTON_PIN NRF_GPIO_PIN_MAP(1,6)  /**< Пин кнопки */
/** @} */

/** @defgroup Конфигурация ШИМ
 *  @{
 */
#define DUTY_MAX 1000       /**< Максимальное значение скважности (100%) */
#define DUTY_STEP 20        /**< Шаг изменения скважности для плавности */
#define DELAY_MS 20         /**< Интервал обновления ШИМ (мс) */
#define DOUBLE_CLICK_MS 300 /**< Таймаут для detection двойного нажатия (мс) */
/** @} */

/** @brief Массив пинов светодиодов */
static const uint32_t leds[3] = {LED_RED, LED_GREEN, LED_BLUE};

/** @brief Последовательность светодиодов и количество миганий */
static const uint8_t sequence_leds[] = {1, 0, 2};     /**< Индексы: зеленый, красный, синий */
static const uint8_t sequence_counts[] = {6, 6, 1};   /**< Количество миганий каждого светодиода */

/** @defgroup Переменные ШИМ
 *  @{
 */
static nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(0);                 /**< Экземпляр ШИМ */
static nrf_pwm_values_common_t pwm_value;                     /**< Значение скважности для общего режима */
static nrf_pwm_sequence_t pwm_seq;                            /**< Последовательность ШИМ */
/** @} */

/** @defgroup Таймеры
 *  @{
 */
APP_TIMER_DEF(double_click_timer); /**< Таймер для detection двойного нажатия */
/** @} */

/** @defgroup Глобальное состояние
 *  @{
 */
static volatile bool blinking_enabled = false;        /**< Флаг активности мигания */
static volatile uint8_t click_count = 0;              /**< Счетчик нажатий в пределах таймаута */
static volatile bool waiting_for_second_click = false; /**< Флаг ожидания второго нажатия */
static uint8_t sequence_index = 0;                    /**< Текущий индекс в последовательности светодиодов */
static uint8_t current_blink = 0;                     /**< Количество завершенных миганий для текущего светодиода */
static uint32_t duty = 0;                             /**< Текущая скважность (0-1000) */
static bool duty_increasing = true;                   /**< Направление изменения скважности */
static uint32_t current_led_pin = LED_GREEN;          /**< Пин текущего активного светодиода */
/** @} */

/**
 * @brief Обработчик таймера двойного нажатия
 * 
 * Вызывается по истечении таймаута DOUBLE_CLICK_MS после первого нажатия.
 * Анализирует количество нажатий и переключает состояние мигания при двойном нажатии.
 * 
 * @param[in] p_context Контекст вызова (не используется)
 */
static void double_click_timeout_handler(void *p_context)
{
    (void)p_context;
    
    if (click_count == 1) {
        /**< Одиночное нажатие - игнорируем */
    } else if (click_count >= 2) {
        /**< Двойное (или более) нажатие - переключаем состояние мигания */
        blinking_enabled = !blinking_enabled;
        
        /**< Визуальная индикация переключения состояния */
        nrf_gpio_cfg_output(current_led_pin);
        for (int i = 0; i < 2; i++) {
            nrf_gpio_pin_write(current_led_pin, 0);
            nrf_delay_ms(80);
            nrf_gpio_pin_write(current_led_pin, 1);
            nrf_delay_ms(80);
        }
    }
    
    /**< Сброс состояния для следующего detection нажатий */
    click_count = 0;
    waiting_for_second_click = false;
}

/**
 * @brief Инициализация системных таймеров
 * 
 * Инициализирует модуль app_timer и создает таймер для detection двойного нажатия.
 */
static void timers_init(void)
{
    ret_code_t err = app_timer_init();
    APP_ERROR_CHECK(err);
    
    err = app_timer_create(&double_click_timer, 
                          APP_TIMER_MODE_SINGLE_SHOT, 
                          double_click_timeout_handler);
    APP_ERROR_CHECK(err);
}

/**
 * @brief Инициализация низкочастотного тактового генератора
 * 
 * Запускает LFCLK, необходимый для работы системных таймеров.
 * Ожидает подтверждения успешного запуска.
 */
static void lfclk_init(void)
{
    NRF_CLOCK->TASKS_LFCLKSTART = 1;
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0) {}
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
}

/**
 * @brief Инициализация ШИМ с одним активным каналом
 * 
 * Настраивает ШИМ для работы с текущим активным светодиодом.
 * Использует общий режим загрузки (COMMON) для одного значения скважности.
 */
static void pwm_init(void)
{
    nrfx_pwm_config_t config;
    
    /**< Конфигурация выходных пинов - только один канал активен */
    config.output_pins[0] = current_led_pin | NRFX_PWM_PIN_INVERTED; /**< Active-low, текущий светодиод */
    config.output_pins[1] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[2] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[3] = NRFX_PWM_PIN_NOT_USED;
    
    config.irq_priority = 6;
    config.base_clock = NRF_PWM_CLK_1MHz;
    config.count_mode = NRF_PWM_MODE_UP;
    config.top_value = DUTY_MAX;
    config.load_mode = NRF_PWM_LOAD_COMMON;  /**< Общий режим для одного значения скважности */
    config.step_mode = NRF_PWM_STEP_AUTO;

    APP_ERROR_CHECK(nrfx_pwm_init(&pwm, &config, NULL));

    /**< Начальное значение - светодиод выключен */
    pwm_value = DUTY_MAX; /**< Active-low: DUTY_MAX = выключено */

    pwm_seq.values.p_common = &pwm_value;
    pwm_seq.length = 1;
    pwm_seq.repeats = 0;
    pwm_seq.end_delay = 0;
}

/**
 * @brief Переключение на следующий светодиод в последовательности
 * 
 * Останавливает текущий ШИМ, переключает на следующий светодиод в последовательности
 * и переинициализирует ШИМ с новым пином. Сохраняет текущую скважность.
 */
static void switch_to_next_led(void)
{
    /**< Остановка и деинициализация текущего ШИМ */
    nrfx_pwm_stop(&pwm, true);
    nrfx_pwm_uninit(&pwm);
    
    /**< Переход к следующему светодиоду в последовательности */
    sequence_index = (sequence_index + 1) % 3;
    uint8_t led_index = sequence_leds[sequence_index];
    current_led_pin = leds[led_index];
    current_blink = 0; /**< Сброс счетчика миганий для нового светодиода */
    
    /**< Переинициализация ШИМ с новым пином */
    nrfx_pwm_config_t config;
    config.output_pins[0] = current_led_pin | NRFX_PWM_PIN_INVERTED;
    config.output_pins[1] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[2] = NRFX_PWM_PIN_NOT_USED;
    config.output_pins[3] = NRFX_PWM_PIN_NOT_USED;
    config.irq_priority = 6;
    config.base_clock = NRF_PWM_CLK_1MHz;
    config.count_mode = NRF_PWM_MODE_UP;
    config.top_value = DUTY_MAX;
    config.load_mode = NRF_PWM_LOAD_COMMON;
    config.step_mode = NRF_PWM_STEP_AUTO;

    APP_ERROR_CHECK(nrfx_pwm_init(&pwm, &config, NULL));
    
    /**< Восстановление текущей скважности для нового светодиода */
    pwm_value = DUTY_MAX - duty;
    nrfx_pwm_simple_playback(&pwm, &pwm_seq, 1, NRFX_PWM_FLAG_LOOP);
}

/**
 * @brief Обработчик событий кнопки через GPIOTE
 * 
 * Вызывается при каждом нажатии/отпускании кнопки.
 * Увеличивает счетчик нажатий и запускает таймер для detection двойного нажатия.
 * 
 * @param[in] pin    Пин кнопки
 * @param[in] action Полярность события
 */
static void button_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    (void)pin;
    (void)action;
    
    /**< Увеличение счетчика нажатий */
    click_count++;
    
    if (!waiting_for_second_click) {
        /**< Первое нажатие - запуск таймера ожидания второго нажатия */
        waiting_for_second_click = true;
        ret_code_t err = app_timer_start(double_click_timer, 
                                       APP_TIMER_TICKS(DOUBLE_CLICK_MS), 
                                       NULL);
        APP_ERROR_CHECK(err);
    }
    /**< Для второго и последующих нажатий таймер уже запущен */
}

/**
 * @brief Инициализация модуля GPIOTE для обработки кнопки
 * 
 * Настраивает кнопку как вход с детектированием переключения состояния
 * и подтяжкой к питанию.
 */
static void gpiote_init(void)
{
    APP_ERROR_CHECK(nrfx_gpiote_init());

    nrfx_gpiote_in_config_t cfg = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
    cfg.pull = NRF_GPIO_PIN_PULLUP;

    APP_ERROR_CHECK(nrfx_gpiote_in_init(BUTTON_PIN, &cfg, button_handler));
    nrfx_gpiote_in_event_enable(BUTTON_PIN, true);
}

/**
 * @brief Обновление скважности ШИМ для текущего светодиода
 * 
 * Устанавливает новое значение скважности и перезапускает воспроизведение
 * последовательности ШИМ для применения изменения.
 */
static void update_pwm(void)
{
    /**< Active-low: duty = 0 → LED OFF, duty = 100% → LED ON */
    pwm_value = DUTY_MAX - duty;
    
    /**< Обновление значения в ШИМ */
    nrfx_pwm_stop(&pwm, false);
    nrfx_pwm_simple_playback(&pwm, &pwm_seq, 1, NRFX_PWM_FLAG_LOOP);
}

/**
 * @brief Выключение всех светодиодов через GPIO
 * 
 * Гарантированно выключает все светодиоды путем прямого управления пинами GPIO.
 * Используется при инициализации и в аварийных ситуациях.
 */
static void turn_off_all_leds(void)
{
    for (uint8_t i = 0; i < 3; i++) {
        nrf_gpio_cfg_output(leds[i]);
        nrf_gpio_pin_write(leds[i], 1); /**< Выключить (active-low) */
    }
}

/**
 * @brief Выключение текущего светодиода через ШИМ
 * 
 * Устанавливает максимальное значение скважности (выключено) для текущего светодиода
 * и применяет изменение через ШИМ.
 */
static void turn_off_current_led(void)
{
    pwm_value = DUTY_MAX; /**< Выключить */
    nrfx_pwm_stop(&pwm, false);
    nrfx_pwm_simple_playback(&pwm, &pwm_seq, 1, NRFX_PWM_FLAG_LOOP);
}

/**
 * @brief Главная функция приложения
 * 
 * Инициализирует все модули, запускает тест светодиодов
 * и входит в бесконечный цикл обработки мигания.
 * 
 * @return int Код возврата (не используется)
 */
int main(void)
{
   
    /**< Инициализация системы */
    lfclk_init();       /**< Низкочастотный тактовый генератор */
    timers_init();      /**< Системные таймеры */
    turn_off_all_leds(); /**< Гарантированное выключение всех светодиодов */

    /**< Инициализация ШИМ (начинаем с зеленого светодиода) */
    current_led_pin = leds[sequence_leds[0]];
    pwm_init();
    turn_off_current_led(); /**< Начальное состояние - выключено */

    /**< Инициализация обработки кнопки */
    gpiote_init();

    /**< Индикация готовности системы */
    nrf_gpio_cfg_output(LED_GREEN);
    for (int i = 0; i < 3; i++) {
        nrf_gpio_pin_write(LED_GREEN, 0);
        nrf_delay_ms(100);
        nrf_gpio_pin_write(LED_GREEN, 1);
        nrf_delay_ms(100);
    }

    /**< Бесконечный цикл обработки мигания */
    while(1)
    {
        if(blinking_enabled)
        {
            /**< Плавное увеличение/уменьшение яркости */
            if(duty_increasing)
            {
                duty += DUTY_STEP;
                if(duty >= DUTY_MAX) {
                    duty = DUTY_MAX;
                    duty_increasing = false;
                }
            }
            else
            {
                duty -= DUTY_STEP;
                if(duty <= 0) {
                    duty = 0;
                    duty_increasing = true;
                    
                    /**< Завершение одного цикла мигания */
                    current_blink++;

                    if(current_blink >= sequence_counts[sequence_index])
                    {
                        /**< Переход к следующему светодиоду в последовательности */
                        switch_to_next_led();
                    }
                }
            }

            update_pwm();
        }
        /**< Если мигание выключено - скважность сохраняется без изменений */

        nrf_delay_ms(DELAY_MS);
    }
}