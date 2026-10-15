#include "minemu/shell.h"

#include "minemu/console.h"
#include "minemu/uart.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Bytes stored before the newline. The newline is the terminator and is
 * not stored. The assignment requires at least 20; the course's own msh
 * tests send 25-byte commands and a 136-byte overlong line, so the
 * capacity sits between those.
 */
#define MSH_LINE_CAPACITY 64U

static int msh_is_backspace(uint8_t byte) {
    return byte == UINT8_C(0x08) || byte == UINT8_C(0x7f);
}

/*
 * length counts every byte the user has typed and not erased, including
 * bytes past capacity that were not stored. Backspace erases those first,
 * so a line that is edited back under capacity is intact and still runs.
 * A line still over capacity at the newline is discarded without output.
 */
static size_t msh_read_line(char *line) {
    size_t length = 0;

    for (;;) {
        uint8_t byte = (uint8_t)minemu_uart0_rx_wait();

        if (byte == (uint8_t)'\n') {
            if (length > MSH_LINE_CAPACITY) {
                return 0;
            }
            return length;
        }
        if (msh_is_backspace(byte)) {
            if (length > 0U) {
                --length;
            }
            continue;
        }
        if (length < MSH_LINE_CAPACITY) {
            line[length] = (char)byte;
        }
        if (length < SIZE_MAX) {
            ++length;
        }
    }
}

static size_t msh_skip_spaces(const char *line, size_t index, size_t length) {
    while (index < length && line[index] == ' ') {
        ++index;
    }
    return index;
}

static size_t msh_word_end(const char *line, size_t index, size_t length) {
    while (index < length && line[index] != ' ') {
        ++index;
    }
    return index;
}

static int msh_word_is(const char *line, size_t start, size_t end, const char *word) {
    size_t index = start;

    while (index < end && *word != '\0' && line[index] == *word) {
        ++index;
        ++word;
    }
    return index == end && *word == '\0';
}

static void msh_put_range(const char *line, size_t start, size_t end) {
    for (size_t index = start; index < end; ++index) {
        minemu_putc(line[index]);
    }
}

/* Arguments are printed separated by one space, however many were typed. */
static void msh_echo(const char *line, size_t index, size_t length) {
    int first = 1;

    for (;;) {
        index = msh_skip_spaces(line, index, length);
        if (index == length) {
            break;
        }
        size_t end = msh_word_end(line, index, length);
        if (!first) {
            minemu_putc(' ');
        }
        msh_put_range(line, index, end);
        first = 0;
        index = end;
    }
    minemu_putc('\n');
}

static void msh_execute(const char *line, size_t length) {
    size_t command = msh_skip_spaces(line, 0, length);
    if (command == length) {
        return;
    }
    size_t command_end = msh_word_end(line, command, length);

    if (msh_word_is(line, command, command_end, "echo")) {
        msh_echo(line, command_end, length);
        return;
    }

    minemu_puts("command not found: ");
    msh_put_range(line, command, command_end);
    minemu_putc('\n');
}

void minemu_shell(void) {
    char line[MSH_LINE_CAPACITY];

    for (;;) {
        minemu_puts("msh> ");
        size_t length = msh_read_line(line);
        msh_execute(line, length);
    }
}
