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

/*
 * UART0 receive path used once the RX interrupt is enabled.
 * minemu_uart_getc reads the hardware FIFO directly and must not be used
 * after minemu_uart0_rx_open: the ISR drains that FIFO into a software queue.
 * The queue is shared with its consumer. minemu_uart0_rx_try_getc pops one
 * byte, or returns -1 at once when the queue is empty; it masks IRQ around
 * the pop and then restores the caller's mask, so it is safe from the SVC
 * handler, which runs with IRQ masked. minemu_uart0_rx_wait spins on it.
 * The ISR runs with IRQ already masked and must not turn IRQ back on.
 */
void minemu_uart0_rx_open(void);
void minemu_uart0_rx_isr(void);
int minemu_uart0_rx_try_getc(void);
uint32_t minemu_uart0_rx_available(void);
int minemu_uart0_rx_wait(void);

#endif
