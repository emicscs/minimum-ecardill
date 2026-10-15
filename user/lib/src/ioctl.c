#include <stdarg.h>
#include <stdint.h>

#include "minemu/ioctl.h"
#include "minemu/user_abi.h"

static int32_t ioctl_svc(int fd, uint32_t request, uint32_t argument0,
                         uint32_t argument1) {
    register uint32_t r0 __asm__("r0") = (uint32_t)fd;
    register uint32_t r1 __asm__("r1") = request;
    register uint32_t r2 __asm__("r2") = argument0;
    register uint32_t r3 __asm__("r3") = argument1;
    register uint32_t r7 __asm__("r7") = MINEMU_SYSCALL_IOCTL;

    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory", "cc");
    return (int32_t)r0;
}

/*
 * Variadic arguments are taken with the type each request declares. An
 * invalid fd/request takes none and is still passed to the kernel, which
 * rejects it.
 */
int32_t ioctl(int fd, uint32_t request, ...) {
    uint32_t argument0 = 0;
    uint32_t argument1 = 0;
    va_list arguments;

    va_start(arguments, request);
    switch (minemu_ioctl_decode(fd, request).action) {
    case MINEMU_IOCTL_ACTION_UART_READ:
    case MINEMU_IOCTL_ACTION_UART_WRITE:
        argument0 = (uint32_t)(uintptr_t)va_arg(arguments, void *);
        argument1 = (uint32_t)va_arg(arguments, int);
        break;
    case MINEMU_IOCTL_ACTION_RNG_NEXT:
    case MINEMU_IOCTL_ACTION_RNG_STATE:
        argument0 = (uint32_t)(uintptr_t)va_arg(arguments, uint32_t *);
        break;
    case MINEMU_IOCTL_ACTION_RNG_SEED:
    case MINEMU_IOCTL_ACTION_TRACE_EVENT:
        argument0 = va_arg(arguments, uint32_t);
        break;
    case MINEMU_IOCTL_ACTION_UART_STATUS:
    case MINEMU_IOCTL_ACTION_INVALID:
    default:
        break;
    }
    va_end(arguments);
    return ioctl_svc(fd, request, argument0, argument1);
}
