#include "usb_cli.h"

#if ESTC_USB_CLI_ENABLED

#include "nrf_cli.h"
#include "nrf_cli_cdc_acm.h"
#include "app_logic.h"
#include "nrf_log.h"
#include "app_usbd.h"
#include "app_usbd_core.h"
#include "app_usbd_serial_num.h"
#include <stdlib.h>

// Настройки CLI
NRF_CLI_CDC_ACM_DEF(m_cli_cdc_acm_transport);

NRF_CLI_DEF(m_cli_cdc_acm,
            "usb_cli:~$ ", 
            &m_cli_cdc_acm_transport.transport,
            '\r', 
            4);

// Обработчики команд

// Команда RGB
static void cmd_rgb(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 4)
    {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: RGB <r> <g> <b>\n");
        return;
    }

    uint32_t r_in = atoi(argv[1]);
    uint32_t g_in = atoi(argv[2]);
    uint32_t b_in = atoi(argv[3]);

    if (r_in > 255 || g_in > 255 || b_in > 255)
    {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Values must be 0-255\n");
        return;
    }

    // Масштабируем 0-255 -> 0-1000
    uint16_t r = (r_in * 1000) / 255;
    uint16_t g = (g_in * 1000) / 255;
    uint16_t b = (b_in * 1000) / 255;
    
    app_logic_set_rgb(r, g, b);
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color set to R=%d G=%d B=%d\n", r_in, g_in, b_in);
}

// Команда HSV
static void cmd_hsv(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 4)
    {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: HSV <h> <s> <v>\n");
        return;
    }
    uint16_t h = atoi(argv[1]);
    uint8_t s = atoi(argv[2]);
    uint8_t v = atoi(argv[3]);
    
    app_logic_set_hsv(h, s, v);
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color set to H=%d S=%d V=%d\n", h, s, v);
}

// Команда help
static void cmd_help(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Supported commands:\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "  RGB <r> <g> <b>   - Set color using RGB values (0-255)\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "  HSV <h> <s> <v>   - Set color using HSV model (H:0-360, S:0-100, V:0-100)\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "  help              - Print information about supported commands\n");
}

// Регистрация команд
NRF_CLI_CMD_REGISTER(RGB, NULL, NULL, cmd_rgb);
NRF_CLI_CMD_REGISTER(HSV, NULL, NULL, cmd_hsv);
NRF_CLI_CMD_REGISTER(help, NULL, NULL, cmd_help);


// Логика USB 
static void usbd_user_ev_handler(app_usbd_event_type_t event)
{
    switch (event)
    {
        case APP_USBD_EVT_STOPPED:
            app_usbd_disable();
            break;
        case APP_USBD_EVT_POWER_DETECTED:
            if (!nrf_drv_usbd_is_enabled())
            {
                app_usbd_enable();
            }
            break;
        case APP_USBD_EVT_POWER_REMOVED:
            app_usbd_stop();
            break;
        case APP_USBD_EVT_POWER_READY:
            app_usbd_start();
            break;
        default:
            break;
    }
}

// Публичные функции

void usb_cli_init(void)
{
    ret_code_t ret;

    ret = nrf_cli_init(&m_cli_cdc_acm, NULL, true, true, NRF_LOG_SEVERITY_INFO);
    APP_ERROR_CHECK(ret);

    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler
    };

    app_usbd_serial_num_generate();
    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);

    app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&nrf_cli_cdc_acm);
    ret = app_usbd_class_append(class_cdc_acm);
    APP_ERROR_CHECK(ret);

    ret = app_usbd_power_events_enable();
    APP_ERROR_CHECK(ret);

    ret = nrf_cli_start(&m_cli_cdc_acm);
    APP_ERROR_CHECK(ret);
}

void usb_cli_process(void)
{
    nrf_cli_process(&m_cli_cdc_acm);
}

#else

void usb_cli_init(void) {}
void usb_cli_process(void) {}

#endif