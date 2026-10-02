#include "minemu/uart.h"

#include "minemu/irq.h"

/*
 * Bytes the RX ISR can hold while the shell is between pops. Hardware
 * accepts more than this; the ISR still reads every ready byte so the
 * UART interrupt deasserts, and drops a byte only when this queue is full.
 */
#define MINEMU_UART0_RX_QUEUE_CAPACITY 128U

struct minemu_uart0_rx_queue {
    volatile uint8_t data[MINEMU_UART0_RX_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
};

static struct minemu_uart0_rx_queue uart0_rx;

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

void minemu_uart0_rx_open(void) {
    uart0_rx.head = 0;
    uart0_rx.tail = 0;
    uart0_rx.count = 0;
    MINEMU_UART0->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
}

void minemu_uart0_rx_isr(void) {
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_RX_READY) != 0U) {
        uint8_t byte = (uint8_t)(MINEMU_UART0->rx_data & UINT32_C(0xff));
        if (uart0_rx.count < MINEMU_UART0_RX_QUEUE_CAPACITY) {
            uint32_t tail = uart0_rx.tail;
            uart0_rx.data[tail] = byte;
            uart0_rx.tail = (tail + 1U) % MINEMU_UART0_RX_QUEUE_CAPACITY;
            uart0_rx.count = uart0_rx.count + 1U;
        }
    }
}

int minemu_uart0_rx_wait(void) {
    for (;;) {
        int byte = -1;

        minemu_irq_disable();
        if (uart0_rx.count > 0U) {
            uint32_t head = uart0_rx.head;
            byte = (int)uart0_rx.data[head];
            uart0_rx.head = (head + 1U) % MINEMU_UART0_RX_QUEUE_CAPACITY;
            uart0_rx.count = uart0_rx.count - 1U;
        }
        minemu_irq_enable();
        if (byte >= 0) {
            return byte;
        }
    }
}
