#ifndef USB_CLI_H
#define USB_CLI_H

// Инициализация USB CLI
void usb_cli_init(void);

// Функция обработки, должна вызываться в основном цикле while(1)
void usb_cli_process(void);

#endif