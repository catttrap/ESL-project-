#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "nrf_gpio.h"

/**
 * @defgroup GPIO Mapping for PCA10059(https://docs.nordicsemi.com/bundle/ug_nrf52840_dongle/page/UG/nrf52840_Dongle/hw_button_led.html)
 * @{
 */
#define LED_RED NRF_GPIO_PIN_MAP(0,6)
#define LED_GREEN NRF_GPIO_PIN_MAP(0,8)
#define LED_BLUE NRF_GPIO_PIN_MAP(0,12)
#define BUTTON_PIN NRF_GPIO_PIN_MAP(1,6)
/** @} */

/** @brief Количество используемых светодиодов */
#define LEDS_COUNT 3

/** @brief Массив пинов светодиодов в порядке [красный, зелёный, синий] */
static const uint32_t leds[LEDS_COUNT] = { LED_RED, LED_GREEN, LED_BLUE };

/**
 * @brief Инициализация GPIO для светодиодов и кнопки.
 *
 * - Светодиоды настраиваются как выходы (active-low).
 * - Кнопка настраивается как вход с подтяжкой вверх.
 */

 void gpio_init(void)
{
    for (int i = 0; i < LEDS_COUNT; i++)
    {
        nrf_gpio_cfg_output(leds[i]);
        nrf_gpio_pin_write(leds[i], 1);  // Выключить (active low)
    }

    nrf_gpio_cfg_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP);
}

/**
 * @brief Включить светодиод.
 * @param led_pin Номер GPIO-пина, подключенного к светодиоду.
 */
void led_on(uint32_t led_pin)
{
    nrf_gpio_pin_write(led_pin, 0); // active low
}

/**
 * @brief Выключить светодиод.
 * @param led_pin Номер GPIO-пина, подключенного к светодиоду.
 */
void led_off(uint32_t led_pin)
{
    nrf_gpio_pin_write(led_pin, 1);
}

/**
 * @brief Мигание заданного светодиода с поддержкой паузы при отпускании кнопки.
 *
 * Если во время свечения кнопка отпущена — светодиод остаётся включённым до следующего нажатия.
 *
 * @param led_pin Пин светодиода.
 * @param count Количество миганий.
 */
void blink_led_with_pause(uint32_t led_pin, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        led_on(led_pin);
        uint32_t elapsed = 0;

        // Пауза с возможностью удержания свечения при отпускании кнопки
        while (elapsed < 300)
        {
            if (nrf_gpio_pin_read(BUTTON_PIN) != 0)
            {
                // Кнопка отпущена — LED остаётся включён
                while (nrf_gpio_pin_read(BUTTON_PIN) != 0)
                {
                    nrf_delay_ms(10);
                }
            }
            nrf_delay_ms(10);
            elapsed += 10;
        }

        led_off(led_pin);
        nrf_delay_ms(300);
    }
}

/**
 * @brief Выполнение последовательности миганий для текущего светодиода.
 *
 * В шаблоне каждая позиция массива соответствует количеству миганий для конкретного светодиода:
 * - LED0 (зелёный): 6 раз
 * - LED1 (красный): 6 раз
 * - LED2 (синий): 1 раз
 *
 * @param led_index Индекс светодиода (0–2).
 */
void execute_pattern_step(uint32_t led_index)
{
    uint32_t blink_pattern[] = {6, 6, 1};

    if (blink_pattern[led_index] > 0)
    {
        blink_led_with_pause(leds[led_index], blink_pattern[led_index]);
    }
}

/**
 * @brief Главная функция приложения.
 *
 * Выполняет:
 *  - Инициализацию GPIO;
 *  - Мониторинг кнопки;
 *  - Пошаговое выполнение шаблона миганий при нажатии;
 *  - Продолжение с того же светодиода после отпускания кнопки.
 */
int main(void)
{
    gpio_init();

    uint32_t current_led = 0;

    while (true)
    {
        bool button_pressed = (nrf_gpio_pin_read(BUTTON_PIN) == 0);

        if (button_pressed)
        {
            execute_pattern_step(current_led);
            current_led = (current_led + 1) % LEDS_COUNT;
        }

        nrf_delay_ms(100);
    }
}