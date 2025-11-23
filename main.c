/* =============================================================
 *  Fully fixed, cleaned and ready-to-build main.c
 *  (Variant №3 — retriggerable debounce + double‑click)
 *  nRF52840 (PCA10059)
 * ============================================================= */

#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrfx_pwm.h"
#include "nrfx_gpiote.h"
#include "app_timer.h"
#include "app_error.h"
#include "math.h"

/* ---------------- Pins ---------------- */
#define LED_RED     NRF_GPIO_PIN_MAP(0,6)    /**< Пин красного светодиода */
#define LED_GREEN   NRF_GPIO_PIN_MAP(0,8)    /**< Пин зеленого светодиода */
#define LED_BLUE    NRF_GPIO_PIN_MAP(0,12)   /**< Пин синего светодиода */
#define BUTTON_PIN  NRF_GPIO_PIN_MAP(1,6)    /**< Пин кнопки (активный низкий уровень) */

/* ---------------- PWM ---------------- */
#define DUTY_MAX   1000  /**< Максимальное значение скважности (100%) для ШИМ 1кГц */
#define DUTY_STEP  50    /**< Шаг изменения скважности при одинарном клике */
#define BREATHE_STEPS 200  /**< Количество шагов для полного цикла дыхания */

/* ---------------- Timings ---------------- */
#define DEBOUNCE_MS        30  /**< Время антидребезга в миллисекундах */
#define DOUBLE_CLICK_MS   300  /**< Таймаут для обнаружения двойного клика */
#define BREATHE_DELAY_MS   16  /**< Период обновления анимации дыхания (~60 FPS) */

/** 
 * @brief Массив пинов светодиодов
 * @note Последовательность для устройства #6601: зеленый → красный → синий
 */
static const uint32_t leds[3] = {LED_GREEN, LED_RED, LED_BLUE};

/**
 * @brief Количество циклов мигания для каждого светодиода
 * @details green:6, red:6, blue:1
 */
static const uint8_t sequence_counts[] = {6, 6, 1};

/* PWM */
static nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(0);          /**< Экземпляр ШИМ */
static nrf_pwm_values_common_t pwm_value;              /**< Текущее значение ШИМ */
static nrf_pwm_sequence_t pwm_seq;                     /**< Последовательность ШИМ */
static bool pwm_playing = false;                       /**< Флаг активности ШИМ */

/* Timers */
APP_TIMER_DEF(debounce_timer);                         /**< Таймер антидребезга */
APP_TIMER_DEF(double_click_timer);                     /**< Таймер двойного клика */
APP_TIMER_DEF(breathe_timer);                          /**< Таймер анимации дыхания */

/* Button FSM */
static volatile bool btn_last_raw = true;              /**< Последнее сырое состояние кнопки */
static volatile bool btn_stable_state = true;          /**< Стабильное состояние после антидребезга */
static volatile bool click_waiting_second = false;     /**< Ожидание второго клика */
static volatile bool press_pending = false;            /**< Ожидание отпускания кнопки */

/**
 * @brief Состояния конечного автомата кликов
 */
typedef enum { 
    CLICK_IDLE,          /**< Ожидание клика */
    CLICK_WAIT_SECOND    /**< Ожидание второго клика для двойного */
} click_state_t;

static click_state_t click_state = CLICK_IDLE;         /**< Текущее состояние кликов */

/* Breathing LED state */
static volatile bool blinking_enabled = false;         /**< Флаг включения автоматического мигания */
static volatile bool breathing_paused = false;         /**< Флаг паузы анимации дыхания */
static uint8_t sequence_index = 0;                     /**< Индекс текущего светодиода в последовательности */
static uint8_t current_blink = 0;                      /**< Счетчик текущих миганий */
static uint32_t current_step = 0;                      /**< Текущий шаг анимации дыхания */
static uint32_t current_led_pin = LED_GREEN;           /**< Пин текущего активного светодиода */
static uint32_t manual_duty = 0;                       /**< Значение скважности для ручного управления */

/* ---------------- Forward decl ---------------- */
static void debounce_timer_handler(void *p_context);
static void double_click_timer_handler(void *p_context);
static void breathe_timer_handler(void *p_context);
static void gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
static void button_stable_event(uint8_t state);
static void handle_single_click(void);
static void handle_double_click(void);
static void switch_to_next_led(void);
static void update_pwm(void);
static uint32_t calculate_sine_duty(uint32_t step);

/**
 * @brief Инициализация низкочастотного тактового генератора
 * @details Необходим для работы модуля app_timer
 */
static void lfclk_init(void)
{
    NRF_CLOCK->TASKS_LFCLKSTART = 1;
    while (!NRF_CLOCK->EVENTS_LFCLKSTARTED) {}
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
}

/**
 * @brief Инициализация таймеров приложения
 * @details Создает три таймера: антидребезг, двойной клик и анимацию дыхания
 */
static void timers_init(void)
{
    app_timer_init();

    app_timer_create(&debounce_timer,
                     APP_TIMER_MODE_SINGLE_SHOT,
                     debounce_timer_handler);

    app_timer_create(&double_click_timer,
                     APP_TIMER_MODE_SINGLE_SHOT,
                     double_click_timer_handler);
                     
    app_timer_create(&breathe_timer,
                     APP_TIMER_MODE_REPEATED,
                     breathe_timer_handler);
}

/**
 * @brief Инициализация ШИМ для указанного пина светодиода
 * @param[in] led_pin Пин светодиода для управления ШИМ
 * @details Настраивает ШИМ с частотой 1кГц и инвертированным выходом
 */
static void pwm_init_for_pin(uint32_t led_pin)
{
    nrfx_pwm_uninit(&pwm);

    nrfx_pwm_config_t cfg = {
        .output_pins = {
            led_pin | NRFX_PWM_PIN_INVERTED,  /**< Инвертированный выход для активного низкого уровня */
            NRFX_PWM_PIN_NOT_USED,
            NRFX_PWM_PIN_NOT_USED,
            NRFX_PWM_PIN_NOT_USED
        },
        .irq_priority = 6,
        .base_clock = NRF_PWM_CLK_1MHz,       /**< Тактовая частота 1МГц для ШИМ 1кГц */
        .count_mode = NRF_PWM_MODE_UP,
        .top_value = DUTY_MAX,                /**< 1000 шагов = 1мс период = 1кГц */
        .load_mode = NRF_PWM_LOAD_COMMON,
        .step_mode = NRF_PWM_STEP_AUTO,
    };

    APP_ERROR_CHECK(nrfx_pwm_init(&pwm, &cfg, NULL));

    pwm_value = DUTY_MAX;  /**< Начальное значение - светодиод выключен (инвертировано) */
    pwm_seq.values.p_common = &pwm_value;
    pwm_seq.length = 1;
    pwm_seq.repeats = 0;
    pwm_seq.end_delay = 0;
    pwm_playing = false;
}

/**
 * @brief Запуск воспроизведения ШИМ
 * @details Запускает циклическое воспроизведение ШИМ последовательности
 */
static void start_pwm_playback(void)
{
    if (!pwm_playing) {
        APP_ERROR_CHECK(nrfx_pwm_simple_playback(&pwm, &pwm_seq, 1, NRFX_PWM_FLAG_LOOP));
        pwm_playing = true;
    }
}

/**
 * @brief Остановка воспроизведения ШИМ
 * @details Полностью останавливает генерацию ШИМ сигнала
 */
static void stop_pwm_playback(void)
{
    if (pwm_playing) {
        nrfx_pwm_stop(&pwm, true);
        pwm_playing = false;
    }
}

/**
 * @brief Обновление значения ШИМ
 * @details Устанавливает новое значение скважности и перезапускает ШИМ
 */
static void update_pwm(void)
{
    if (blinking_enabled && !breathing_paused) {
        /** Автоматическое дыхание - используем синусоидальную функцию */
        pwm_value = DUTY_MAX - calculate_sine_duty(current_step);
    } else {
        /** Ручное управление - используем установленную скважность */
        pwm_value = DUTY_MAX - manual_duty;
    }
    
    if (pwm_playing) {
        /** Перезапуск ШИМ с новым значением скважности */
        nrfx_pwm_stop(&pwm, false);
        nrfx_pwm_simple_playback(&pwm, &pwm_seq, 1, NRFX_PWM_FLAG_LOOP);
    }
}

/**
 * @brief Расчет скважности для плавного дыхания
 * @param[in] step Текущий шаг анимации (0-BREATHE_STEPS)
 * @return Значение скважности для ШИМ
 * @details Использует синусоидальную функцию с квадратом для естественного дыхания
 */
static uint32_t calculate_sine_duty(uint32_t step)
{
    /** Нормализация шага в диапазон 0-2π */
    float angle = (2.0f * 3.14159265f * step) / BREATHE_STEPS;
    
    /** Синусоида от 0 до 1 */
    float sine_value = (1.0f - cosf(angle)) / 2.0f;
    
    /** Квадрат синусоиды для более естественного дыхания */
    sine_value = sine_value * sine_value;
    
    return (uint32_t)(sine_value * DUTY_MAX);
}

/**
 * @brief Переключение на следующий светодиод в последовательности
 * @details Останавливает ШИМ, переключает светодиод и переинициализирует ШИМ
 */
static void switch_to_next_led(void)
{
    /** Остановка ШИМ перед переключением светодиода */
    stop_pwm_playback();

    /** Переход к следующему светодиоду в последовательности */
    sequence_index = (sequence_index + 1) % 3;
    current_led_pin = leds[sequence_index];
    current_blink = 0;
    current_step = 0;

    /** Переинициализация ШИМ для нового пина светодиода */
    pwm_init_for_pin(current_led_pin);
    
    /** Запуск ШИМ */
    update_pwm();
    start_pwm_playback();
}

/**
 * @brief Обработчик таймера анимации дыхания
 * @param[in] p_context Контекст таймера (не используется)
 * @details Обновляет шаг анимации и проверяет завершение циклов мигания
 */
static void breathe_timer_handler(void *p_context)
{
    /** Выход если анимация отключена или на паузе */
    if (!blinking_enabled || breathing_paused) {
        return;
    }

    /** Обновление шага анимации дыхания */
    current_step = (current_step + 1) % BREATHE_STEPS;
    
    /** Проверка завершения полного цикла дыхания */
    if (current_step == 0) {
        current_blink++;
        /** Проверка достижения лимита миганий для текущего светодиода */
        if (current_blink >= sequence_counts[sequence_index]) {
            switch_to_next_led();
        }
    }
    
    update_pwm();
}

/**
 * @brief Обработчик таймера антидребезга
 * @param[in] p_context Контекст таймера (не используется)
 * @details Читает текущее состояние кнопки и обновляет стабильное состояние
 */
static void debounce_timer_handler(void *p_context)
{
    bool raw = nrf_gpio_pin_read(BUTTON_PIN);

    if (raw != btn_stable_state) {
        btn_stable_state = raw;
        button_stable_event(raw);
    }
}

/**
 * @brief Обработчик таймаута двойного клика
 * @param[in] p_context Контекст таймера (не используется)
 * @details Вызывается при таймауте двойного клика - обрабатывает одинарный клик
 */
static void double_click_timer_handler(void *p_context)
{
    /** Таймаут двойного клика - обработка одинарного клика */
    handle_single_click();
    
    click_state = CLICK_IDLE;
    click_waiting_second = false;
}

/**
 * @brief Обработчик одинарного клика
 * @details Изменяет скважность ШИМ. В автоматическом режиме переводит в паузу.
 */
static void handle_single_click(void)
{
    /** В автоматическом режиме - пауза и переход к ручному управлению */
    if (blinking_enabled && !breathing_paused) {
        breathing_paused = true;
        app_timer_stop(breathe_timer);
        
        /** Сохранение текущей скважности для ручного управления */
        manual_duty = calculate_sine_duty(current_step);
    }
    
    /** Изменение скважности (циклическое от 0% до 100%) */
    manual_duty += DUTY_STEP;
    if (manual_duty > DUTY_MAX) {
        manual_duty = 0;
    }
    
    /** Визуальная обратная связь - короткое мигание */
    nrf_gpio_pin_write(current_led_pin, 0);
    nrf_delay_ms(30);
    nrf_gpio_pin_write(current_led_pin, 1);
    nrf_delay_ms(30);
    
    update_pwm();
    if (!pwm_playing) {
        start_pwm_playback();
    }
}

/**
 * @brief Обработчик двойного клика
 * @details Переключает между автоматическим и ручным режимом работы
 */
static void handle_double_click(void)
{
    /** Переключение режима работы */
    blinking_enabled = !blinking_enabled;
    
    if (blinking_enabled) {
        /** Включение автоматического дыхания */
        breathing_paused = false;
        current_step = 0;
        current_blink = 0;
        
        /** Запуск таймера анимации */
        app_timer_start(breathe_timer, APP_TIMER_TICKS(BREATHE_DELAY_MS), NULL);
        
        /** Визуальная обратная связь - быстрое мигание */
        for (int i = 0; i < 3; i++) {
            nrf_gpio_pin_write(current_led_pin, 0);
            nrf_delay_ms(50);
            nrf_gpio_pin_write(current_led_pin, 1);
            nrf_delay_ms(50);
        }
    } else {
        /** Выключение автоматического дыхания - переход в ручной режим */
        breathing_paused = true;
        app_timer_stop(breathe_timer);
        
        /** Визуальная обратная связь - долгое мигание */
        nrf_gpio_pin_write(current_led_pin, 0);
        nrf_delay_ms(200);
        nrf_gpio_pin_write(current_led_pin, 1);
        nrf_delay_ms(100);
    }
    
    update_pwm();
    if (!pwm_playing) {
        start_pwm_playback();
    }
}

/**
 * @brief Обработчик события нажатия кнопки
 * @details Определяет тип клика (одинарный/двойной) и запускает соответствующий обработчик
 */
static void handle_button_click(void)
{
    if (click_state == CLICK_IDLE) {
        /** Первый клик - ожидание потенциального двойного клика */
        click_state = CLICK_WAIT_SECOND;
        click_waiting_second = true;
        app_timer_start(double_click_timer, APP_TIMER_TICKS(DOUBLE_CLICK_MS), NULL);
    }
    else if (click_state == CLICK_WAIT_SECOND) {
        /** Обнаружен двойной клик */
        app_timer_stop(double_click_timer);
        handle_double_click();
        click_state = CLICK_IDLE;
        click_waiting_second = false;
    }
}

/**
 * @brief Обработчик стабильного состояния кнопки
 * @param[in] state Стабильное состояние кнопки (0=нажата, 1=отпущена)
 * @details Вызывается после антидребезга, обрабатывает только отпускание кнопки
 */
static void button_stable_event(uint8_t state)
{
    if (state == 0) {
        /** Кнопка нажата - установка флага ожидания */
        press_pending = true;
    }
    else {
        /** Кнопка отпущена - обработка клика */
        if (press_pending) {
            handle_button_click();
            press_pending = false;
        }
    }
}

/**
 * @brief Обработчик прерывания GPIOTE
 * @param[in] pin Пин вызвавший прерывание
 * @param[in] action Тип изменения пина
 * @details Запускает таймер антидребезга при изменении состояния кнопки
 */
static void gpiote_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    bool raw = nrf_gpio_pin_read(BUTTON_PIN);

    if (raw != btn_last_raw) {
        btn_last_raw = raw;
        app_timer_start(debounce_timer, APP_TIMER_TICKS(DEBOUNCE_MS), NULL);
    }
}

/**
 * @brief Инициализация модуля GPIOTE
 * @details Настраивает прерывание по изменению состояния кнопки с подтяжкой к питанию
 */
static void gpiote_init(void)
{
    if (!nrfx_gpiote_is_init()) {
        nrfx_gpiote_init();
    }

    nrfx_gpiote_in_config_t cfg = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
    cfg.pull = NRF_GPIO_PIN_PULLUP;

    nrfx_gpiote_in_init(BUTTON_PIN, &cfg, gpiote_handler);
    nrfx_gpiote_in_event_enable(BUTTON_PIN, true);
}

/**
 * @brief Выключение всех светодиодов
 * @details Настраивает все пины светодиодов как выходы и выключает их
 */
static void turn_off_all_leds(void)
{
    for (uint8_t i = 0; i < 3; i++) {
        nrf_gpio_cfg_output(leds[i]);
        nrf_gpio_pin_write(leds[i], 1); /**< Светодиоды с активным низким уровнем, 1 = выключено */
    }
}

/**
 * @brief Главная функция программы
 * @return Код возврата (не возвращается)
 * @details Инициализирует все модули и запускает основной цикл обработки
 */
int main(void)
{
    /** Инициализация тактового генератора */
    lfclk_init();
    
    /** Инициализация таймеров */
    timers_init();
    
    /** Выключение всех светодиодов */
    turn_off_all_leds();

    /** Инициализация начального состояния - зеленый светодиод */
    sequence_index = 0;
    current_led_pin = leds[sequence_index];
    pwm_init_for_pin(current_led_pin);

    /** Инициализация обработки кнопки */
    gpiote_init();

    /** Индикация запуска - тройное мигание зеленым */
    for (int i = 0; i < 3; i++) {
        nrf_gpio_pin_write(LED_GREEN, 0);
        nrf_delay_ms(150);
        nrf_gpio_pin_write(LED_GREEN, 1);
        nrf_delay_ms(150);
    }

    /** Начальное состояние - ручной режим с нулевой скважностью */
    manual_duty = 0;
    blinking_enabled = false;
    breathing_paused = true;
    update_pwm();
    start_pwm_playback();

    /** Основной цикл программы */
    while (1) {
        /** Основная работа выполняется в обработчиках прерываний и таймеров */
        nrf_delay_ms(100);
    }
}