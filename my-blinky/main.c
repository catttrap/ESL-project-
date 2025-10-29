#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "boards.h"

/**
 * @brief Blink a specific LED multiple times
 * @param led_index Index of the LED to blink
 * @param blink_count Number of times to blink
 */
void blink_led_multiple_times(uint32_t led_index, uint32_t blink_count)
{
    for (uint32_t i = 0; i < blink_count; i++)
    {
        // Turn LED on
        bsp_board_led_on(led_index);
        nrf_delay_ms(300);
        
        // Turn LED off
        bsp_board_led_off(led_index);
        nrf_delay_ms(300);
    }
}

/**
 * @brief Execute the blink pattern based on ID 6601
 */
void execute_blink_pattern(void)
{
    // ID 6601 interpreted as: LED0=6 blinks, LED1=6 blinks, LED2=0 blinks, LED3=1 blink
    uint32_t blink_pattern[] = {6, 6, 0, 1};
    uint32_t pause_between_leds = 1000; // 1 second pause between different LEDs
    
    for (int i = 0; i < LEDS_NUMBER; i++)
    {
        if (blink_pattern[i] > 0)
        {
            blink_led_multiple_times(i, blink_pattern[i]);
        }
        
        // Pause between different LEDs (except after the last one)
        if (i < LEDS_NUMBER - 1)
        {
            nrf_delay_ms(pause_between_leds);
        }
    }
}

/**
 * @brief Function for application main entry.
 */
int main(void)
{
    /* Configure board. */
    bsp_board_init(BSP_INIT_LEDS);
    
    // Turn off all LEDs initially
    for (int i = 0; i < LEDS_NUMBER; i++)
    {
        bsp_board_led_off(i);
    }

    /* Execute blink pattern in loop */
    while (true)
    {
        execute_blink_pattern();
        
        // Long pause between complete sequences
        nrf_delay_ms(3000);
    }
}

/** @} */