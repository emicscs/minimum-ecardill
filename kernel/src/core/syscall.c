#include "minemu/syscall.h"

#include <stdbool.h>
#include <stdint.h>

#include "minemu/ioctl.h"
#include "minemu/platform.h"
#include "minemu/trace.h"
#include "minemu/uart.h"
#include "minemu/user.h"
#include "minemu/user_abi.h"

#define CPSR_MODE_MASK UINT32_C(0x1f)
#define CPSR_MODE_USR UINT32_C(0x10)
#define CPSR_THUMB UINT32_C(0x20)
#define A32_SVC_ZERO UINT32_C(0xef000000)

/* Bounce buffer size for UART transfers; any length is accepted. */
#define IOCTL_CHUNK 64U

static volatile struct minemu_uart_regs *uart_for(enum minemu_ioctl_device device) {
    return device == MINEMU_IOCTL_DEVICE_UART1 ? MINEMU_UART1 : MINEMU_UART0;
}

/*
 * UART0 input is drained by its RX interrupt into a software queue, so it is
 * read from there. UART1 has no interrupt enabled and is read straight from
 * the device FIFO. Neither path waits for input.
 */
static int uart_take_byte(enum minemu_ioctl_device device) {
    if (device == MINEMU_IOCTL_DEVICE_UART0) {
        return minemu_uart0_rx_try_getc();
    }
    return minemu_uart_getc(MINEMU_UART1);
}

static int32_t ioctl_uart_read(enum minemu_ioctl_device device, uint32_t destination,
                               int32_t count) {
    if (count < 0 || !minemu_user_range_ok(destination, (uint32_t)count, MINEMU_PTE_WRITABLE)) {
        return MINEMU_IOCTL_ERROR;
    }
    uint8_t bytes[IOCTL_CHUNK];
    uint32_t total = 0;
    uint32_t remaining = (uint32_t)count;
    while (remaining != 0) {
        uint32_t want = remaining < IOCTL_CHUNK ? remaining : IOCTL_CHUNK;
        uint32_t got = 0;
        while (got < want) {
            int byte = uart_take_byte(device);
            if (byte < 0) {
                break;
            }
            bytes[got++] = (uint8_t)byte;
        }
        if (got != 0 && !minemu_user_copy_to(destination + total, bytes, got)) {
            return MINEMU_IOCTL_ERROR;
        }
        total += got;
        remaining -= got;
        if (got < want) {
            break;
        }
    }
    return (int32_t)total;
}

static int32_t ioctl_uart_write(enum minemu_ioctl_device device, uint32_t source,
                                int32_t count) {
    if (count < 0 || !minemu_user_range_ok(source, (uint32_t)count, MINEMU_PTE_READABLE)) {
        return MINEMU_IOCTL_ERROR;
    }
    volatile struct minemu_uart_regs *uart = uart_for(device);
    uint8_t bytes[IOCTL_CHUNK];
    uint32_t done = 0;
    while (done < (uint32_t)count) {
        uint32_t chunk = (uint32_t)count - done;
        if (chunk > IOCTL_CHUNK) {
            chunk = IOCTL_CHUNK;
        }
        (void)minemu_user_copy_from(bytes, source + done, chunk, MINEMU_PTE_READABLE);
        minemu_uart_write(uart, bytes, chunk);
        done += chunk;
    }
    return (int32_t)done;
}

static int32_t ioctl_uart_status(enum minemu_ioctl_device device) {
    uint32_t status = uart_for(device)->status;
    bool rx_ready = device == MINEMU_IOCTL_DEVICE_UART0
                        ? minemu_uart0_rx_available() != 0U
                        : (status & MINEMU_UART_STATUS_RX_READY) != 0U;
    int32_t result = 0;
    if (rx_ready) {
        result |= MINEMU_IOCTL_UART_RX_READY;
    }
    if ((status & MINEMU_UART_STATUS_TX_READY) != 0U) {
        result |= MINEMU_IOCTL_UART_TX_READY;
    }
    return result;
}

/* Checks the destination before reading the register: a DATA read advances the RNG. */
static int32_t ioctl_rng_read(uint32_t destination, bool advance) {
    if (!minemu_user_range_ok(destination, sizeof(uint32_t), MINEMU_PTE_WRITABLE)) {
        return MINEMU_IOCTL_ERROR;
    }
    uint32_t value = advance ? MINEMU_RNG->data : MINEMU_RNG->state;
    return minemu_user_copy_to(destination, &value, sizeof(value)) ? 0 : MINEMU_IOCTL_ERROR;
}

static int32_t sys_ioctl(int32_t fd, uint32_t op, uint32_t argument0, uint32_t argument1) {
    struct minemu_ioctl_target target = minemu_ioctl_decode(fd, op);

    switch (target.action) {
    case MINEMU_IOCTL_ACTION_UART_READ:
        return ioctl_uart_read(target.device, argument0, (int32_t)argument1);
    case MINEMU_IOCTL_ACTION_UART_WRITE:
        return ioctl_uart_write(target.device, argument0, (int32_t)argument1);
    case MINEMU_IOCTL_ACTION_UART_STATUS:
        return ioctl_uart_status(target.device);
    case MINEMU_IOCTL_ACTION_RNG_SEED:
        MINEMU_RNG->seed = argument0;
        return 0;
    case MINEMU_IOCTL_ACTION_RNG_NEXT:
        return ioctl_rng_read(argument0, true);
    case MINEMU_IOCTL_ACTION_RNG_STATE:
        return ioctl_rng_read(argument0, false);
    case MINEMU_IOCTL_ACTION_TRACE_EVENT:
        minemu_trace_event(argument0);
        return 0;
    case MINEMU_IOCTL_ACTION_INVALID:
    default:
        return MINEMU_IOCTL_ERROR;
    }
}

/*
 * Only an A32 `svc #0` issued from USR mode is a syscall. The instruction is
 * fetched through the user page table, so a bogus return address cannot make
 * the kernel read outside user memory.
 */
static bool svc_from_user(const struct minemu_trap_frame *frame) {
    uint32_t instruction;
    return frame->exception_id == MINEMU_EXCEPTION_SVC &&
           (frame->spsr & CPSR_MODE_MASK) == CPSR_MODE_USR &&
           (frame->spsr & CPSR_THUMB) == 0U &&
           minemu_user_copy_from(&instruction, frame->fault_pc, sizeof(instruction),
                                 MINEMU_PTE_EXECUTABLE) &&
           instruction == A32_SVC_ZERO;
}

struct minemu_trap_frame *minemu_svc_dispatch(struct minemu_trap_frame *frame) {
    if (!svc_from_user(frame)) {
        frame->r[0] = (uint32_t)MINEMU_IOCTL_ERROR;
        return frame;
    }

    switch (frame->r[7]) {
    case MINEMU_SYSCALL_IOCTL:
        frame->r[0] = (uint32_t)sys_ioctl((int32_t)frame->r[0], frame->r[1], frame->r[2],
                                          frame->r[3]);
        break;
    default:
        frame->r[0] = (uint32_t)MINEMU_IOCTL_ERROR;
        break;
    }
    return frame;
}
