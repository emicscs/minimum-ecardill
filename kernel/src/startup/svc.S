.syntax unified
.cpu cortex-a9
.arm

/*
 * SVC entry. Hardware has copied CPSR to SPSR_svc, switched to SVC mode with
 * IRQ masked, and set LR_svc to the instruction after the svc. This builds
 * the 88-byte struct minemu_trap_frame from kernel/include/minemu/trap.h on
 * the SVC stack, hands it to minemu_svc_dispatch, and resumes the caller
 * from whatever frame the dispatcher returns. The dispatcher reports a
 * result by rewriting the saved r0.
 *
 * Linked as its own object ahead of libminemu_kernel.a so this strong
 * definition replaces the weak fail-stop stub in exception.c.
 */
.global minemu_svc_trampoline
.type minemu_svc_trampoline, %function
.extern minemu_svc_dispatch

minemu_svc_trampoline:
    sub sp, sp, #88
    stmia sp, {r0-r12}
    str lr, [sp, #52]
    mrs r1, spsr
    str r1, [sp, #56]
    ldr r1, =-3
    str r1, [sp, #60]
    sub r1, lr, #4
    str r1, [sp, #64]
    mov r1, #0
    str r1, [sp, #68]
    str r1, [sp, #72]
    /* USR SP and LR are banked; the ^ form stores the user copies. */
    add r1, sp, #76
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #84]
    mov r0, sp
    bl minemu_svc_dispatch
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
    movs pc, lr
