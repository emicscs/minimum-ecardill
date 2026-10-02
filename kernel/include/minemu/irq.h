#ifndef MINEMU_IRQ_H
#define MINEMU_IRQ_H

#include "minemu/trap.h"

/* Students implement this A32 vector trampoline and its IRQ dispatch policy. */
void minemu_irq_trampoline(void) __attribute__((noreturn));
struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame);

/* Unmask only the UART0 source. Other sources stay disabled. */
void minemu_irq_enable_uart0(void);

static inline void minemu_irq_enable(void) {
    __asm__ volatile("cpsie i" : : : "memory");
}

static inline void minemu_irq_disable(void) {
    __asm__ volatile("cpsid i" : : : "memory");
}

#endif
