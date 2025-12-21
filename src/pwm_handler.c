#include "pwm_handler.h"
#include "nrfx_pwm.h"
#include "nrf_pwm.h"
#include "app_timer.h"
#include "nrf_log.h"

#define DUTY_MAX               1000 /**< Максимальное значение скважности (100%) для ШИМ 1кГц */
#define SLOW_BLINK_PERIOD_MS   500 /**< Период медленного мигания в мс */  
#define FAST_BLINK_PERIOD_MS   100  /**< Период быстрого мигания в мс */

APP_TIMER_DEF(m_blink_timer);

static nrfx_pwm_t m_pwm_instance = NRFX_PWM_INSTANCE(0);
static nrf_pwm_values_individual_t m_seq_values;

static pwm_indicator_mode_t m_indicator_mode = PWM_INDICATOR_OFF;
static bool m_blink_state = false;

// Таймер мигания
static void blink_timer_handler(void *p_context)
{
    // Инвертируем состояние для мигания
    m_blink_state = !m_blink_state;
    m_seq_values.channel_0 = m_blink_state ? DUTY_MAX : 0;
}

// Инициализация ШИМ
void pwm_handler_init(const uint32_t *led_pins)
{
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG;
    
    // Настройка пинов
    for (int i = 0; i < 4; i++) {
        config.output_pins[i] = (led_pins[i] != NRF_PWM_PIN_NOT_CONNECTED) ? led_pins[i] : NRFX_PWM_PIN_NOT_USED;
    }

    config.top_value = DUTY_MAX;
    config.load_mode = NRF_PWM_LOAD_INDIVIDUAL; 
    config.step_mode = NRF_PWM_STEP_AUTO;

    nrfx_pwm_init(&m_pwm_instance, &config, NULL);

    // Сброс значений каналов
    m_seq_values.channel_0 = 0;
    m_seq_values.channel_1 = 0;
    m_seq_values.channel_2 = 0;
    m_seq_values.channel_3 = 0;

    app_timer_create(&m_blink_timer, APP_TIMER_MODE_REPEATED, blink_timer_handler);
    
    nrf_pwm_sequence_t seq;
    seq.values.p_individual = &m_seq_values;
    seq.length              = 4;
    seq.repeats             = 0;
    seq.end_delay           = 0;
    // Запуск циклического воспроизведения
    nrfx_pwm_simple_playback(&m_pwm_instance, &seq, 1, NRFX_PWM_FLAG_LOOP);
}

// Установка RGB
void pwm_handler_set_rgb(uint16_t r, uint16_t g, uint16_t b)
{
    m_seq_values.channel_1 = (r > DUTY_MAX) ? DUTY_MAX : r;
    m_seq_values.channel_2 = (g > DUTY_MAX) ? DUTY_MAX : g;
    m_seq_values.channel_3 = (b > DUTY_MAX) ? DUTY_MAX : b;
    NRF_LOG_DEBUG("PWM R=%d G=%d B=%d", r, g, b);
}

// Устанавливает режим работы индикатора (мигание/постоянный)
void pwm_handler_set_indicator_mode(pwm_indicator_mode_t mode)
{
    m_indicator_mode = mode;
    app_timer_stop(m_blink_timer);

    switch (mode)
    {
        case PWM_INDICATOR_OFF:
            m_seq_values.channel_0 = 0;
            break;
        case PWM_INDICATOR_ON:
            m_seq_values.channel_0 = DUTY_MAX;
            break;
        case PWM_INDICATOR_BLINK_SLOW:
            app_timer_start(m_blink_timer, APP_TIMER_TICKS(SLOW_BLINK_PERIOD_MS), NULL);
            break;
        case PWM_INDICATOR_BLINK_FAST:
            app_timer_start(m_blink_timer, APP_TIMER_TICKS(FAST_BLINK_PERIOD_MS), NULL);
            break;
    }
    
}