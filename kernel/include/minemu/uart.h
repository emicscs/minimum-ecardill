#ifndef MINEMU_UART_H
#define MINEMU_UART_H

#include "minemu/platform.h"

/*
 * Polled UART driver. Every register access is one aligned 32-bit MMIO
 * operation through struct minemu_uart_regs. Transmit waits until TX ready,
 * then writes the byte in the low 8 bits with the high bits clear.
 * Receive does not wait: minemu_uart_getc returns -1 when no byte is waiting.
 * minemu_uart_puts does not append a newline.
 */
void minemu_uart_putc(volatile struct minemu_uart_regs *uart, uint8_t byte);
void minemu_uart_write(volatile struct minemu_uart_regs *uart, const void *data,
                       size_t length);
void minemu_uart_puts(volatile struct minemu_uart_regs *uart, const char *text);
int minemu_uart_getc(volatile struct minemu_uart_regs *uart);

#endif
