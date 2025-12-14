#include "app_timer.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_gpio.h"

#include "button_handler.h"
#include "pwm_handler.h"
#include "app_logic.h"
#include "usb_cli.h"

/* ---------------- Pins ---------------- */
#define INDICATOR_LED_PIN NRF_GPIO_PIN_MAP(0,6)
#define LED_RED     NRF_GPIO_PIN_MAP(0,8)    /**< Пин красного светодиода */
#define LED_GREEN   NRF_GPIO_PIN_MAP(1,9)    /**< Пин зеленого светодиода */
#define LED_BLUE    NRF_GPIO_PIN_MAP(0,12)   /**< Пин синего светодиода */
#define BUTTON_PIN  NRF_GPIO_PIN_MAP(1,6)    /**< Пин кнопки (активный низкий уровень) */

static const int sequence_counts[4] = { 6, 6, 0, 1 };

static const uint32_t led_pins[4] = {
    INDICATOR_LED_PIN,
    LED_RED,
    LED_GREEN,
    LED_BLUE
};

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

int main(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
    while (!nrf_drv_clock_lfclk_is_running());

    err_code = nrf_drv_power_init(NULL);
    APP_ERROR_CHECK(err_code);

    log_init();
    
    app_timer_init();

    pwm_handler_init(led_pins);

    button_handler_init(BUTTON_PIN);

    app_logic_init(sequence_counts);

    usb_cli_init(); 

    while (1)
    {
        usb_cli_process();

        if (NRF_LOG_PROCESS() == false)
        {
            __WFE();
        }
    }
}