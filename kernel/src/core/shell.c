#include "minemu/shell.h"

#include "minemu/console.h"
#include "minemu/uart.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Bytes stored before the newline. The newline is the terminator and is
 * not stored. A 21st byte latches overflow: further input except newline
 * is discarded, and the line is not executed.
 */
#define MSH_LINE_CAPACITY 20U

static int msh_is_backspace(uint8_t byte) {
    return byte == UINT8_C(0x08) || byte == UINT8_C(0x7f);
}

static void msh_read_line(char *line, size_t *length_out) {
    size_t length = 0;
    int overflow = 0;

    for (;;) {
        uint8_t byte = (uint8_t)minemu_uart0_rx_wait();

        if (byte == (uint8_t)'\n') {
            if (overflow) {
                line[0] = '\0';
                *length_out = 0;
            } else {
                line[length] = '\0';
                *length_out = length;
            }
            return;
        }
        if (msh_is_backspace(byte)) {
            if (!overflow && length > 0U) {
                --length;
                line[length] = '\0';
            }
            continue;
        }
        if (overflow || length == MSH_LINE_CAPACITY) {
            overflow = 1;
            continue;
        }
        line[length] = (char)byte;
        ++length;
    }
}

static int msh_is_echo(const char *line, size_t start, size_t command_length) {
    return command_length == 4U && line[start] == 'e' && line[start + 1] == 'c' &&
           line[start + 2] == 'h' && line[start + 3] == 'o';
}

static void msh_execute(const char *line, size_t length) {
    size_t index = 0;

    while (index < length && line[index] == ' ') {
        ++index;
    }
    if (index == length) {
        return;
    }

    size_t command = index;
    while (index < length && line[index] != ' ') {
        ++index;
    }
    size_t command_length = index - command;

    if (msh_is_echo(line, command, command_length)) {
        while (index < length && line[index] == ' ') {
            ++index;
        }
        while (index < length) {
            minemu_putc(line[index]);
            ++index;
        }
        minemu_putc('\n');
        return;
    }

    minemu_puts("command not found: ");
    for (size_t cursor = command; cursor < command + command_length; ++cursor) {
        minemu_putc(line[cursor]);
    }
    minemu_putc('\n');
}

void minemu_shell(void) {
    char line[MSH_LINE_CAPACITY + 1U];

    for (;;) {
        minemu_puts("msh> ");
        size_t length = 0;
        msh_read_line(line, &length);
        msh_execute(line, length);
    }
}
