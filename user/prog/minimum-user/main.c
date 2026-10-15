#include <stdint.h>

#include "minemu/ioctl.h"

void minemu_user_main(void) {
    static const char message[] = "hello world\n";
    (void)ioctl(MINEMU_IOCTL_FD_UART0, MINEMU_IOCTL_OP_UART_WRITE, (void *)message,
                (int)(sizeof(message) - 1U));

    for (;;) {
        __asm__ volatile("nop");
    }
}
