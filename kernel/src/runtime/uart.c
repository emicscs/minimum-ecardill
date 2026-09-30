#include "minemu/uart.h"

void minemu_uart_putc(volatile struct minemu_uart_regs *uart, uint8_t byte) {
    while ((uart->status & MINEMU_UART_STATUS_TX_READY) == 0U) {
    }
    uart->tx_data = (uint32_t)byte;
}

void minemu_uart_write(volatile struct minemu_uart_regs *uart, const void *data,
                       size_t length) {
    const uint8_t *bytes = data;
    for (size_t index = 0; index < length; ++index) {
        minemu_uart_putc(uart, bytes[index]);
    }
}

void minemu_uart_puts(volatile struct minemu_uart_regs *uart, const char *text) {
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        minemu_uart_putc(uart, (uint8_t)*text);
        ++text;
    }
}

int minemu_uart_getc(volatile struct minemu_uart_regs *uart) {
    if ((uart->status & MINEMU_UART_STATUS_RX_READY) == 0U) {
        return -1;
    }
    return (int)(uart->rx_data & UINT32_C(0xff));
}
