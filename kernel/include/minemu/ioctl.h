#ifndef MINEMU_IOCTL_H
#define MINEMU_IOCTL_H

#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * ioctl request decoding shared by the user library and the kernel, so both
 * sides agree on which variadic arguments a request carries.
 *
 * Two fd/op numberings are accepted:
 *
 *   Device-addressed (HW2 handout): fd is the device's MMIO base address and
 *   op is a small per-device number.
 *     RNG   0x10003000          op 0 rng_set_seed(unsigned seed)
 *                               op 1 rng_get_data(unsigned *data)
 *     UART0 0x10004000, UART1 0x10005000
 *                               op 0 uart_read(char *dst, int n)
 *                               op 1 uart_write(char *src, int n)
 *     TRACE 0x1000f000          op 0 trace_event(unsigned value)
 *
 *   Descriptor-style (minemu/user_abi.h, used by tests/hw2): fd is one of
 *   MINEMU_FD_* and op is one of MINEMU_IOCTL_* (0x101 ... 0x301).
 *   MINEMU_FD_UART_IN is read-only UART0, MINEMU_FD_UART_OUT is write-only
 *   UART0, and MINEMU_FD_UART_DIAG is UART1.
 *
 * The numberings do not overlap, and either op form is accepted on either
 * fd form as long as the operation belongs to that device.
 */
#define MINEMU_IOCTL_FD_RNG 0x10003000
#define MINEMU_IOCTL_FD_UART0 0x10004000
#define MINEMU_IOCTL_FD_UART1 0x10005000
#define MINEMU_IOCTL_FD_TRACE 0x1000f000

#define MINEMU_IOCTL_OP_RNG_SET_SEED 0x0
#define MINEMU_IOCTL_OP_RNG_GET_DATA 0x1
#define MINEMU_IOCTL_OP_UART_READ 0x0
#define MINEMU_IOCTL_OP_UART_WRITE 0x1
#define MINEMU_IOCTL_OP_TRACE_EVENT 0x0

enum minemu_ioctl_device {
    MINEMU_IOCTL_DEVICE_NONE = 0,
    MINEMU_IOCTL_DEVICE_UART0,
    MINEMU_IOCTL_DEVICE_UART1,
    MINEMU_IOCTL_DEVICE_RNG,
    MINEMU_IOCTL_DEVICE_TRACE,
};

enum minemu_ioctl_action {
    MINEMU_IOCTL_ACTION_INVALID = 0,
    MINEMU_IOCTL_ACTION_UART_READ,
    MINEMU_IOCTL_ACTION_UART_WRITE,
    MINEMU_IOCTL_ACTION_UART_STATUS,
    MINEMU_IOCTL_ACTION_RNG_SEED,
    MINEMU_IOCTL_ACTION_RNG_NEXT,
    MINEMU_IOCTL_ACTION_RNG_STATE,
    MINEMU_IOCTL_ACTION_TRACE_EVENT,
};

struct minemu_ioctl_target {
    enum minemu_ioctl_device device;
    enum minemu_ioctl_action action;
};

static inline struct minemu_ioctl_target minemu_ioctl_decode(int fd, uint32_t op) {
    struct minemu_ioctl_target target = {MINEMU_IOCTL_DEVICE_NONE,
                                          MINEMU_IOCTL_ACTION_INVALID};
    int can_read = 1;
    int can_write = 1;

    switch (fd) {
    case MINEMU_IOCTL_FD_UART0:
        target.device = MINEMU_IOCTL_DEVICE_UART0;
        break;
    case MINEMU_FD_UART_IN:
        target.device = MINEMU_IOCTL_DEVICE_UART0;
        can_write = 0;
        break;
    case MINEMU_FD_UART_OUT:
        target.device = MINEMU_IOCTL_DEVICE_UART0;
        can_read = 0;
        break;
    case MINEMU_IOCTL_FD_UART1:
    case MINEMU_FD_UART_DIAG:
        target.device = MINEMU_IOCTL_DEVICE_UART1;
        break;
    case MINEMU_IOCTL_FD_RNG:
    case MINEMU_FD_RNG:
        target.device = MINEMU_IOCTL_DEVICE_RNG;
        break;
    case MINEMU_IOCTL_FD_TRACE:
    case MINEMU_FD_TRACE:
        target.device = MINEMU_IOCTL_DEVICE_TRACE;
        break;
    default:
        return target;
    }

    int is_uart = target.device == MINEMU_IOCTL_DEVICE_UART0 ||
                  target.device == MINEMU_IOCTL_DEVICE_UART1;
    int is_rng = target.device == MINEMU_IOCTL_DEVICE_RNG;
    int is_trace = target.device == MINEMU_IOCTL_DEVICE_TRACE;
    enum minemu_ioctl_action action = MINEMU_IOCTL_ACTION_INVALID;

    switch (op) {
    case 0x0:
        action = is_uart    ? MINEMU_IOCTL_ACTION_UART_READ
                 : is_rng   ? MINEMU_IOCTL_ACTION_RNG_SEED
                            : MINEMU_IOCTL_ACTION_TRACE_EVENT;
        break;
    case 0x1:
        action = is_uart  ? MINEMU_IOCTL_ACTION_UART_WRITE
                 : is_rng ? MINEMU_IOCTL_ACTION_RNG_NEXT
                          : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_UART_READ:
        action = is_uart ? MINEMU_IOCTL_ACTION_UART_READ : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_UART_WRITE:
        action = is_uart ? MINEMU_IOCTL_ACTION_UART_WRITE : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_UART_STATUS:
        action = is_uart ? MINEMU_IOCTL_ACTION_UART_STATUS : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_RNG_NEXT:
        action = is_rng ? MINEMU_IOCTL_ACTION_RNG_NEXT : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_RNG_STATE:
        action = is_rng ? MINEMU_IOCTL_ACTION_RNG_STATE : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_RNG_SEED:
        action = is_rng ? MINEMU_IOCTL_ACTION_RNG_SEED : MINEMU_IOCTL_ACTION_INVALID;
        break;
    case MINEMU_IOCTL_TRACE_EVENT:
        action = is_trace ? MINEMU_IOCTL_ACTION_TRACE_EVENT : MINEMU_IOCTL_ACTION_INVALID;
        break;
    default:
        break;
    }

    if ((action == MINEMU_IOCTL_ACTION_UART_READ && !can_read) ||
        (action == MINEMU_IOCTL_ACTION_UART_WRITE && !can_write)) {
        action = MINEMU_IOCTL_ACTION_INVALID;
    }
    target.action = action;
    return target;
}

#endif
