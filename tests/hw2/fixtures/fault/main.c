#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * MMIO is mapped supervisor-only, so a direct register store from USR mode
 * must abort into the kernel instead of reaching the device. The kernel
 * traces 0x0fa17001 (data abort) and stops; the poke value never appears.
 */
void minemu_user_main(void) {
    (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, UINT32_C(0xa2f00001));
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0x1000f000) = UINT32_C(0xa2f0dead);
    (void)ioctl(MINEMU_FD_TRACE, MINEMU_IOCTL_TRACE_EVENT, UINT32_C(0xa2f00002));
    for (;;) {
    }
}
