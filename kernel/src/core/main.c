#include "minemu/boot.h"
#include "minemu/console.h"
#include "minemu/irq.h"
#include "minemu/trap.h"
#include "minemu/trace.h"
#include "minemu/uart.h"
#include "minemu/user.h"

#define TRACE_BAD_BOOT_INFO UINT32_C(0xb007bad0)
#define TRACE_BAD_USER_MODULE UINT32_C(0xb007bad1)

/* The image packages the first user program under this module name. */
static const char init_module[] = "minimum-user";

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000)) {
        minemu_trace_event(TRACE_BAD_BOOT_INFO);
        minemu_fail_stop();
    }

    uint32_t entry;
    if (!minemu_user_load(boot_info, init_module, &entry)) {
        minemu_trace_event(TRACE_BAD_USER_MODULE);
        minemu_printf("minimum: cannot load user module %s\n", init_module);
        minemu_fail_stop();
    }

    /* IRQ stays masked in SVC mode; entering USR mode unmasks it. */
    minemu_uart0_rx_open();
    minemu_irq_enable_uart0();
    minemu_enter_user(entry, MINEMU_USER_STACK_TOP);
}
