.syntax unified
.cpu cortex-a9
.arm

.global minemu_enter_user
.type minemu_enter_user, %function

/*
 * r0 is the A32 entry VA; r1 is the aligned top of the user stack.
 * SPSR is set to plain USR mode (0x10): A32, IRQ and FIQ unmasked, so the
 * UART0 receive interrupt keeps running while user code executes.
 */
minemu_enter_user:
    cpsid if
    mov r2, r0

    /* Privileged user-register transfer initializes USR's banked SP and LR. */
    sub sp, sp, #8
    str r1, [sp]
    mov r3, #0
    str r3, [sp, #4]
    mov r3, sp
    ldmia r3, {sp, lr}^
    /* An ldm with ^ must not be followed immediately by a banked-register use. */
    nop
    add sp, sp, #8

    mov r3, #0x10
    msr spsr_cxsf, r3
    mov lr, r2
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    movs pc, lr
