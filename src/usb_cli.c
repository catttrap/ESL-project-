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
#include <string.h>

/**
 * @brief Определение транспорта CLI для CDC ACM
 */
NRF_CLI_CDC_ACM_DEF(m_cli_cdc_acm_transport);

/**
 * @brief Определение экземпляра CLI
 */
NRF_CLI_DEF(m_cli_cdc_acm,
            "usb_cli:~$ ", 
            &m_cli_cdc_acm_transport.transport,
            '\r', 
            4);

/**
 * @brief Обработка команды RGB
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
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

/**
 * @brief Обработка команды HSV
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
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

/**
 * @brief Обработка команды add_rgb_color
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_add_rgb_color(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 5) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_rgb_color <r> <g> <b> <name>\n");
        return;
    }
    
    uint16_t r = (atoi(argv[1]) * 1000) / 255;
    uint16_t g = (atoi(argv[2]) * 1000) / 255;
    uint16_t b = (atoi(argv[3]) * 1000) / 255;

     if (r > 1000 || g > 1000 || b > 1000) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: RGB values must be 0-255\n");
        return;
    }
    
    if (strlen(argv[4]) >= 32) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color name too long (max 31 chars)\n");
        return;
    }
    
    if (app_logic_save_color_rgb(r, g, b, argv[4])) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color '%s' saved.\n", argv[4]);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists.\n", argv[4]);
    }
}

/**
 * @brief Обработка команды add_hsv_color
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_add_hsv_color(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 5) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_hsv_color <h> <s> <v> <name>\n");
        return;
    }
    
    uint16_t h = atoi(argv[1]);
    uint8_t s = atoi(argv[2]);
    uint8_t v = atoi(argv[3]);

     if (h > 360 || s > 100 || v > 100) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: H must be 0-360, S and V must be 0-100\n");
        return;
    }
    
    if (strlen(argv[4]) >= 32) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color name too long (max 31 chars)\n");
        return;
    }
    
    if (app_logic_save_color_hsv(h, s, v, argv[4])) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color '%s' saved.\n", argv[4]);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists.\n", argv[4]);
    }
}

/**
 * @brief Обработка команды add_current_color
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_add_current_color(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_current_color <color_name>\n");
        return;
    }
    
    if (app_logic_save_current_color(argv[1])) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Current color saved as '%s'.\n", argv[1]);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists.\n", argv[1]);
    }
}

/**
 * @brief Обработка команды del_color
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_del_color(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: del_color <color_name>\n");
        return;
    }
    if (app_logic_del_color(argv[1])) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Deleted '%s'.\n", argv[1]);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Not found: '%s'.\n", argv[1]);
    }
}

/**
 * @brief Обработка команды apply_color
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_apply_color(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: apply_color <color_name>\n");
        return;
    }
    if (app_logic_apply_color(argv[1])) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Applied color '%s'.\n", argv[1]);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' not found\n", argv[1]);
    }
}

/**
 * @brief Обработка команды list_colors
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_list_colors(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    uint8_t count = 0;
    const saved_color_entry_t * list = app_logic_get_list(&count);
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Saved colors (%d maximum):\n", MAX_SAVED_COLORS);
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "----------------------------------------\n");

    for (int i = 0; i < count; i++) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "%d) %s (H:%d S:%d V:%d)\n", 
                        i+1, list[i].name, list[i].color.h, list[i].color.s, list[i].color.v);
    }
}

/**
 * @brief Обработка команды help
 * @param p_cli Указатель на экземпляр CLI
 * @param argc Количество аргументов
 * @param argv Массив строк аргументов
 */
static void cmd_help(nrf_cli_t const * p_cli, size_t argc, char ** argv)
{
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Supported commands:\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " RGB <r> <g> <b> - Set color using RGB values (0-255)\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " HSV <h> <s> <v> - Set color using HSV model (H:0-360, S:0-100, V:0-100)\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " add_rgb_color <r> <g> <b> <name> - Save RGB color with name\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " add_hsv_color <h> <s> <v> <name> - Save HSV color with name\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " add_current_color <name> - Save current color with name\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " del_color <name> - Delete saved color\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " apply_color <name> - Apply saved color\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " list_colors - List all saved colors\n");
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " help - Print information about supported commands\n");
}

// Регистрация команд
NRF_CLI_CMD_REGISTER(RGB, NULL, NULL, cmd_rgb);
NRF_CLI_CMD_REGISTER(HSV, NULL, NULL, cmd_hsv);
NRF_CLI_CMD_REGISTER(add_rgb_color, NULL, NULL, cmd_add_rgb_color);
NRF_CLI_CMD_REGISTER(add_hsv_color, NULL, NULL, cmd_add_hsv_color);
NRF_CLI_CMD_REGISTER(add_current_color, NULL, NULL, cmd_add_current_color);
NRF_CLI_CMD_REGISTER(del_color, NULL, NULL, cmd_del_color);
NRF_CLI_CMD_REGISTER(apply_color, NULL, NULL, cmd_apply_color);
NRF_CLI_CMD_REGISTER(list_colors, NULL, NULL, cmd_list_colors);
NRF_CLI_CMD_REGISTER(help, NULL, NULL, cmd_help);

/**
 * @brief Обработчик событий USB
 * @param event Тип события USB
 */
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

/**
 * @brief Инициализация USB CLI
 */
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

/**
 * @brief Обработка USB CLI
 */
void usb_cli_process(void)
{
    nrf_cli_process(&m_cli_cdc_acm);
}

#else

void usb_cli_init(void) {}
void usb_cli_process(void) {}

#endif