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

// Настройки CLI
NRF_CLI_CDC_ACM_DEF(m_cli_cdc_acm_transport);
NRF_CLI_DEF(m_cli_cdc_acm, "usb_cli:~$ ", &m_cli_cdc_acm_transport.transport, '\r', 4);

// Структура для хранения цвета с именем
typedef struct {
    char name[32];          // Имя цвета
    uint16_t r;            // Красный компонент (0-1000)
    uint16_t g;            // Зеленый компонент (0-1000)
    uint16_t b;            // Синий компонент (0-1000)
    uint16_t h;            // Оттенок (0-360)
    uint8_t s;             // Насыщенность (0-100)
    uint8_t v;             // Яркость (0-100)
    bool valid;            // Флаг валидности записи
} saved_color_t;

// Максимальное количество сохраняемых цветов
#define MAX_SAVED_COLORS 10

// Массив для хранения цветов
static saved_color_t saved_colors[MAX_SAVED_COLORS];

// Инициализация массива цветов
static void init_saved_colors(void) {
    for (int i = 0; i < MAX_SAVED_COLORS; i++) {
        saved_colors[i].valid = false;
        saved_colors[i].name[0] = '\0';
    }
}

// Поиск свободного слота для сохранения цвета
static int find_free_slot(void) {
    for (int i = 0; i < MAX_SAVED_COLORS; i++) {
        if (!saved_colors[i].valid) {
            return i;
        }
    }
    return -1; // Нет свободных слотов
}

// Поиск цвета по имени
static int find_color_by_name(const char* name) {
    for (int i = 0; i < MAX_SAVED_COLORS; i++) {
        if (saved_colors[i].valid && strcmp(saved_colors[i].name, name) == 0) {
            return i;
        }
    }
    return -1; // Цвет не найден
}

// Масштабирование RGB из 0-255 в 0-1000
static uint16_t scale_rgb(uint32_t value) {
    return (value * 1000) / 255;
}

// Обратный масштаб RGB из 0-1000 в 0-255
static uint8_t unscale_rgb(uint16_t value) {
    return (value * 255) / 1000;
}

// Обработчики команд

// Команда RGB
static void cmd_rgb(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 4) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: RGB <r> <g> <b>\n");
        return;
    }
    
    uint32_t r_in = atoi(argv[1]);
    uint32_t g_in = atoi(argv[2]);
    uint32_t b_in = atoi(argv[3]);
    
    if (r_in > 255 || g_in > 255 || b_in > 255) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Values must be 0-255\n");
        return;
    }
    
    // Масштабируем 0-255 -> 0-1000
    uint16_t r = scale_rgb(r_in);
    uint16_t g = scale_rgb(g_in);
    uint16_t b = scale_rgb(b_in);
    
    app_logic_set_rgb(r, g, b);
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color set to R=%d G=%d B=%d\n", r_in, g_in, b_in);
}

// Команда HSV
static void cmd_hsv(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 4) {
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
static void cmd_help(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
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

// Команда add_rgb_color
static void cmd_add_rgb_color(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 5) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_rgb_color <r> <g> <b> <color_name>\n");
        return;
    }
    
    uint32_t r_in = atoi(argv[1]);
    uint32_t g_in = atoi(argv[2]);
    uint32_t b_in = atoi(argv[3]);
    const char* name = argv[4];
    
    if (r_in > 255 || g_in > 255 || b_in > 255) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: RGB values must be 0-255\n");
        return;
    }
    
    if (strlen(name) >= 32) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color name too long (max 31 chars)\n");
        return;
    }
    
    // Проверяем, существует ли уже цвет с таким именем
    if (find_color_by_name(name) != -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists\n", name);
        return;
    }
    
    // Ищем свободный слот
    int slot = find_free_slot();
    if (slot == -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Maximum number of saved colors reached (%d)\n", MAX_SAVED_COLORS);
        return;
    }
    
    // Сохраняем цвет
    strncpy(saved_colors[slot].name, name, sizeof(saved_colors[slot].name) - 1);
    saved_colors[slot].name[sizeof(saved_colors[slot].name) - 1] = '\0';
    saved_colors[slot].r = scale_rgb(r_in);
    saved_colors[slot].g = scale_rgb(g_in);
    saved_colors[slot].b = scale_rgb(b_in);
    saved_colors[slot].valid = true;
    
    // Для HSV устанавливаем значения по умолчанию (0)
    saved_colors[slot].h = 0;
    saved_colors[slot].s = 0;
    saved_colors[slot].v = 0;
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "RGB color '%s' saved successfully\n", name);
}

// Команда add_hsv_color
static void cmd_add_hsv_color(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 5) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_hsv_color <h> <s> <v> <color_name>\n");
        return;
    }
    
    uint16_t h = atoi(argv[1]);
    uint8_t s = atoi(argv[2]);
    uint8_t v = atoi(argv[3]);
    const char* name = argv[4];
    
    if (h > 360 || s > 100 || v > 100) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: H must be 0-360, S and V must be 0-100\n");
        return;
    }
    
    if (strlen(name) >= 32) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color name too long (max 31 chars)\n");
        return;
    }
    
    // Проверяем, существует ли уже цвет с таким именем
    if (find_color_by_name(name) != -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists\n", name);
        return;
    }
    
    // Ищем свободный слот
    int slot = find_free_slot();
    if (slot == -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Maximum number of saved colors reached (%d)\n", MAX_SAVED_COLORS);
        return;
    }
    
    // Сохраняем цвет
    strncpy(saved_colors[slot].name, name, sizeof(saved_colors[slot].name) - 1);
    saved_colors[slot].name[sizeof(saved_colors[slot].name) - 1] = '\0';
    saved_colors[slot].h = h;
    saved_colors[slot].s = s;
    saved_colors[slot].v = v;
    saved_colors[slot].valid = true;
    
    // Для RGB устанавливаем значения по умолчанию (0)
    // В реальном приложении здесь должна быть конверсия HSV->RGB
    saved_colors[slot].r = 0;
    saved_colors[slot].g = 0;
    saved_colors[slot].b = 0;
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "HSV color '%s' saved successfully\n", name);
}

// Команда add_current_color
static void cmd_add_current_color(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: add_current_color <color_name>\n");
        return;
    }
    
    const char* name = argv[1];
    
    if (strlen(name) >= 32) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color name too long (max 31 chars)\n");
        return;
    }
    
    // Проверяем, существует ли уже цвет с таким именем
    if (find_color_by_name(name) != -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' already exists\n", name);
        return;
    }
    
    // Ищем свободный слот
    int slot = find_free_slot();
    if (slot == -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Maximum number of saved colors reached (%d)\n", MAX_SAVED_COLORS);
        return;
    }
    
    strncpy(saved_colors[slot].name, name, sizeof(saved_colors[slot].name) - 1);
    saved_colors[slot].name[sizeof(saved_colors[slot].name) - 1] = '\0';
    
    saved_colors[slot].r = 500;  
    saved_colors[slot].g = 500;  
    saved_colors[slot].b = 500;  
    saved_colors[slot].h = 180;  
    saved_colors[slot].s = 50;   
    saved_colors[slot].v = 50;   
    saved_colors[slot].valid = true;
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Current color saved as '%s'\n", name);
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Note: In real implementation, actual color values would be retrieved from LED controller\n");
}

// Команда del_color
static void cmd_del_color(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: del_color <color_name>\n");
        return;
    }
    
    const char* name = argv[1];
    int index = find_color_by_name(name);
    
    if (index == -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' not found\n", name);
        return;
    }
    
    // Удаляем цвет
    saved_colors[index].valid = false;
    saved_colors[index].name[0] = '\0';
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Color '%s' deleted successfully\n", name);
}

// Команда apply_color
static void cmd_apply_color(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    if (argc != 2) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Usage: apply_color <color_name>\n");
        return;
    }
    
    const char* name = argv[1];
    int index = find_color_by_name(name);
    
    if (index == -1) {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' not found\n", name);
        return;
    }
    
    // Применяем цвет
    // Сначала пытаемся применить как RGB
    if (saved_colors[index].r != 0 || saved_colors[index].g != 0 || saved_colors[index].b != 0) {
        app_logic_set_rgb(saved_colors[index].r, saved_colors[index].g, saved_colors[index].b);
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Applied RGB color '%s': R=%d G=%d B=%d\n", 
                       name, 
                       unscale_rgb(saved_colors[index].r),
                       unscale_rgb(saved_colors[index].g),
                       unscale_rgb(saved_colors[index].b));
    }
    // Иначе применяем как HSV
    else if (saved_colors[index].h != 0 || saved_colors[index].s != 0 || saved_colors[index].v != 0) {
        app_logic_set_hsv(saved_colors[index].h, saved_colors[index].s, saved_colors[index].v);
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Applied HSV color '%s': H=%d S=%d V=%d\n", 
                       name, 
                       saved_colors[index].h,
                       saved_colors[index].s,
                       saved_colors[index].v);
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Error: Color '%s' has invalid data\n", name);
    }
}

// Команда list_colors
static void cmd_list_colors(nrf_cli_t const * p_cli, size_t argc, char ** argv) {
    (void)argc; // Неиспользуемый параметр
    (void)argv; // Неиспользуемый параметр
    
    int count = 0;
    
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Saved colors (%d maximum):\n", MAX_SAVED_COLORS);
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "----------------------------------------\n");
    
    for (int i = 0; i < MAX_SAVED_COLORS; i++) {
        if (saved_colors[i].valid) {
            count++;
            nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "%d. %s: ", count, saved_colors[i].name);
            
            if (saved_colors[i].r != 0 || saved_colors[i].g != 0 || saved_colors[i].b != 0) {
                nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "RGB(%d, %d, %d)", 
                               unscale_rgb(saved_colors[i].r),
                               unscale_rgb(saved_colors[i].g),
                               unscale_rgb(saved_colors[i].b));
            }
            
            if (saved_colors[i].h != 0 || saved_colors[i].s != 0 || saved_colors[i].v != 0) {
                if (saved_colors[i].r != 0 || saved_colors[i].g != 0 || saved_colors[i].b != 0) {
                    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, " / ");
                }
                nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "HSV(%d, %d%%, %d%%)", 
                               saved_colors[i].h,
                               saved_colors[i].s,
                               saved_colors[i].v);
            }
            
            nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "\n");
        }
    }
    
    if (count == 0) {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "No colors saved\n");
    } else {
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "----------------------------------------\n");
        nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "Total: %d color(s)\n", count);
    }
}

// Регистрация команд
NRF_CLI_CMD_REGISTER(RGB, NULL, NULL, cmd_rgb);
NRF_CLI_CMD_REGISTER(HSV, NULL, NULL, cmd_hsv);
NRF_CLI_CMD_REGISTER(help, NULL, NULL, cmd_help);
NRF_CLI_CMD_REGISTER(add_rgb_color, NULL, "Save RGB color with name", cmd_add_rgb_color);
NRF_CLI_CMD_REGISTER(add_hsv_color, NULL, "Save HSV color with name", cmd_add_hsv_color);
NRF_CLI_CMD_REGISTER(add_current_color, NULL, "Save current color with name", cmd_add_current_color);
NRF_CLI_CMD_REGISTER(del_color, NULL, "Delete saved color", cmd_del_color);
NRF_CLI_CMD_REGISTER(apply_color, NULL, "Apply saved color", cmd_apply_color);
NRF_CLI_CMD_REGISTER(list_colors, NULL, "List all saved colors", cmd_list_colors);

// Логика USB
static void usbd_user_ev_handler(app_usbd_event_type_t event) {
    switch (event) {
        case APP_USBD_EVT_STOPPED:
            app_usbd_disable();
            break;
        case APP_USBD_EVT_POWER_DETECTED:
            if (!nrf_drv_usbd_is_enabled()) {
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
void usb_cli_init(void) {
    ret_code_t ret;
    
    // Инициализация массива сохраненных цветов
    init_saved_colors();
    
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

void usb_cli_process(void) {
    nrf_cli_process(&m_cli_cdc_acm);
}

#else

void usb_cli_init(void) {}
void usb_cli_process(void) {}

#endif