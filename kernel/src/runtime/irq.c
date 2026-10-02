#include "minemu/irq.h"

#include "minemu/platform.h"
#include "minemu/uart.h"

/*
 * One handler per interrupt-controller source id. Source ids are the
 * shift amounts in minemu/platform.h (0 through 3). A source with no
 * work still returns so dispatch can write EOI.
 */
static void minemu_irq_ignore(void) {}

static void (*const minemu_irq_handlers[])(void) = {
    minemu_irq_ignore,
    minemu_uart0_rx_isr,
    minemu_irq_ignore,
    minemu_irq_ignore,
};

_Static_assert(sizeof(minemu_irq_handlers) / sizeof(minemu_irq_handlers[0]) == 4U,
               "one handler per interrupt source");

void minemu_irq_enable_uart0(void) {
    MINEMU_INTERRUPT->enable = UINT32_C(1) << MINEMU_IRQ_UART0;
}

struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame) {
    uint32_t source = (uint32_t)frame->exception_id;

    if (source < sizeof(minemu_irq_handlers) / sizeof(minemu_irq_handlers[0])) {
        minemu_irq_handlers[source]();
    }
    MINEMU_INTERRUPT->eoi = source;
    return frame;
}
