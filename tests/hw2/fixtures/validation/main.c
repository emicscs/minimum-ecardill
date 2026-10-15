#include <stdint.h>

#include "minemu/ioctl.h"
#include "minemu/user_abi.h"

/*
 * Each check emits PASS(n) or FAIL(n) through the device-addressed TRACE
 * ioctl, so validation.toml can assert the exact list of passes and a
 * failure shows up as 0xbad000nn in the trace log.
 */
#define PASS(n) (UINT32_C(0xa2e00000) | (uint32_t)(n))
#define FAIL(n) (UINT32_C(0xbad00000) | (uint32_t)(n))

#define USER_STACK_TOP UINT32_C(0x00800000)
/* The kernel maps 4 stack pages and leaves the page below them unmapped. */
#define USER_STACK_GUARD UINT32_C(0x007fb000)
#define KERNEL_TEXT UINT32_C(0xc0030000)
#define KERNEL_DIRECT UINT32_C(0xc0000000)
#define UART0_MMIO UINT32_C(0x10004000)

/* No zero-initialized globals: user.ld marks .bss NOLOAD. */
static void check(unsigned *number, int ok) {
    ++*number;
    (void)ioctl(MINEMU_IOCTL_FD_TRACE, MINEMU_IOCTL_OP_TRACE_EVENT,
                ok ? PASS(*number) : FAIL(*number));
}

static int32_t raw_syscall(uint32_t number) {
    register uint32_t r0 __asm__("r0") = 0;
    register uint32_t r7 __asm__("r7") = number;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory", "cc");
    return (int32_t)r0;
}

static void idle(void) {
    __asm__ volatile("bkpt #0");
    for (;;) {
    }
}

void minemu_user_main(void) {
    unsigned n = 0;
    uint32_t value = 0;
    uint32_t state_before = 0;
    uint32_t state_after = 0;
    uint32_t first = 0;
    uint32_t second = 0;
    char buffer[16];
    char big_buffer[300];

    /* 1-3: both numberings reach the same RNG. */
    check(&n, ioctl(MINEMU_IOCTL_FD_RNG, MINEMU_IOCTL_OP_RNG_SET_SEED, UINT32_C(0xcafef00d)) == 0);
    check(&n, ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &value) == 0 &&
                  value == UINT32_C(0xcafef00d));
    check(&n, ioctl(MINEMU_IOCTL_FD_RNG, MINEMU_IOCTL_OP_RNG_GET_DATA, &first) == 0 &&
                  ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_SEED, UINT32_C(0xcafef00d)) == 0 &&
                  ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, &second) == 0 && first == second);

    /* 4-6: UART writes in both numberings, plus UART1. */
    check(&n, ioctl(MINEMU_IOCTL_FD_UART0, MINEMU_IOCTL_OP_UART_WRITE, (void *)"ok\n", 3) == 3);
    check(&n, ioctl(MINEMU_IOCTL_FD_UART1, MINEMU_IOCTL_OP_UART_WRITE, (void *)"diag\n", 5) == 5);
    check(&n, ioctl(MINEMU_FD_UART_DIAG, MINEMU_IOCTL_UART_WRITE, (void *)"diag\n", 5) == 5);

    /* 7-8: reads never block; zero-length transfers are no-ops. */
    check(&n, ioctl(MINEMU_IOCTL_FD_UART0, MINEMU_IOCTL_OP_UART_READ, (void *)buffer,
                    (int)sizeof(buffer)) == 0);
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, 0) == 0);

    /* 9: a write longer than the kernel's bounce buffer is fully delivered. */
    for (unsigned index = 0; index < sizeof(big_buffer) - 1U; ++index) {
        big_buffer[index] = 'x';
    }
    big_buffer[sizeof(big_buffer) - 1U] = '\n';
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)big_buffer,
                    (int)sizeof(big_buffer)) == (int32_t)sizeof(big_buffer));

    /* 10-14: invalid fd, op, and op/device mismatches. */
    check(&n, ioctl(99, MINEMU_IOCTL_UART_WRITE, (void *)"x", 1) < 0);
    check(&n, ioctl(0x10006000, 0) < 0);
    check(&n, ioctl(MINEMU_IOCTL_FD_RNG, 7) < 0);
    check(&n, ioctl(MINEMU_IOCTL_FD_TRACE, 1) < 0);
    check(&n, ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_RNG_NEXT, &value) < 0);

    /* 15-16: stdin is read-only and stdout is write-only. */
    check(&n, ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_WRITE, (void *)"x", 1) < 0);
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_READ, (void *)buffer, 1) < 0);

    /* 17-21: pointers outside user memory, into the guard page, or off the end. */
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)0, 1) < 0);
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)KERNEL_TEXT, 4) < 0);
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)UART0_MMIO, 4) < 0);
    check(&n, ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)USER_STACK_GUARD, 4) < 0);
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE,
                    (void *)(USER_STACK_TOP - 2U), 8) < 0);

    /* 22: negative length. */
    check(&n, ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)"x", -1) < 0);

    /* 23-24: a rejected RNG read must not advance the generator. */
    check(&n, ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &state_before) == 0 &&
                  ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_NEXT, (uint32_t *)KERNEL_DIRECT) < 0);
    check(&n, ioctl(MINEMU_FD_RNG, MINEMU_IOCTL_RNG_STATE, &state_after) == 0 &&
                  state_before == state_after);

    /* 25: status reports TX ready. */
    check(&n, (ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_STATUS) &
               MINEMU_IOCTL_UART_TX_READY) != 0);

    /* 26-27: unknown syscall numbers fail and return to the caller. */
    check(&n, raw_syscall(0) == MINEMU_IOCTL_ERROR);
    check(&n, raw_syscall(99) == MINEMU_IOCTL_ERROR);

    idle();
}
