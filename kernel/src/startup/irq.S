.syntax unified
.cpu cortex-a9
.arm

/*
 * IRQ entry. Hardware saves SPSR, switches to IRQ mode, masks IRQ, and
 * branches here with LR = interrupted PC + 4. This trampoline builds the
 * trap frame from kernel/include/minemu/trap.h and returns with the
 * sequence in kernel/examples/irq-context-switch/irq.S.
 *
 * Linked as its own object ahead of libminemu_kernel.a so this strong
 * definition replaces the weak fail-stop stub in exception.c.
 */
.global minemu_irq_trampoline
.type minemu_irq_trampoline, %function
.extern minemu_irq_dispatch

minemu_irq_trampoline:
    sub sp, sp, #88
    stmia sp, {r0-r12}
    str lr, [sp, #52]
    mrs r1, spsr
    str r1, [sp, #56]
    ldr r1, =0x10000000
    ldr r1, [r1, #8]
    str r1, [sp, #60]
    sub r1, lr, #4
    str r1, [sp, #64]
    mov r1, #0
    str r1, [sp, #68]
    str r1, [sp, #72]
    add r1, sp, #76
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #84]
    mov r0, sp
    bl minemu_irq_dispatch
    mov sp, r0
    ldr lr, [sp, #52]
    ldr r1, [sp, #56]
    msr spsr_cxsf, r1
    add r1, sp, #76
    ldmia r1, {sp, lr}^
    /* An ldm with ^ must not be followed immediately by a banked-register use. */
    nop
    ldmia sp, {r0-r12}
    add sp, sp, #88
    subs pc, lr, #4
