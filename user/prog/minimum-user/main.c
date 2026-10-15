#include <stddef.h>
#include <stdint.h>

#include "minemu/user_abi.h"

/*
 * msh: the user-mode shell. All console I/O goes through the ioctl syscall
 * on MINEMU_FD_UART_IN / MINEMU_FD_UART_OUT; this program has no access to
 * the UART registers.
 */

/*
 * Bytes stored before the newline. The newline is the terminator and is
 * not stored. The assignment requires at least 20; the course's own msh
 * tests send 25-byte commands and a 136-byte overlong line, so the
 * capacity sits between those.
 */
#define MSH_LINE_CAPACITY 64U
#define MSH_INPUT_CHUNK 32U

/*
 * Bytes fetched by one uart_read that the line editor has not consumed yet.
 * Lives on the stack: the user linker script marks .bss NOLOAD, so the
 * program keeps no zero-initialized globals.
 */
struct msh_input {
    char bytes[MSH_INPUT_CHUNK];
    size_t length;
    size_t next;
};

static void msh_write(const char *bytes, size_t length) {
    while (length != 0U) {
        int32_t written = ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)bytes,
                                (int)length);
        if (written <= 0) {
            return;
        }
        bytes += written;
        length -= (size_t)written;
    }
}

static void msh_puts(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    msh_write(text, length);
}

/* uart_read never blocks, so poll until it hands back at least one byte. */
static uint8_t msh_getc(struct msh_input *input) {
    while (input->next == input->length) {
        int32_t count = ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)input->bytes,
                              (int)sizeof(input->bytes));
        if (count > 0) {
            input->length = (size_t)count;
            input->next = 0;
        }
    }
    return (uint8_t)input->bytes[input->next++];
}

static int msh_is_backspace(uint8_t byte) {
    return byte == UINT8_C(0x08) || byte == UINT8_C(0x7f);
}

/*
 * length counts every byte the user has typed and not erased, including
 * bytes past capacity that were not stored. Backspace erases those first,
 * so a line that is edited back under capacity is intact and still runs.
 * A line still over capacity at the newline is discarded without output.
 */
static size_t msh_read_line(struct msh_input *input, char *line) {
    size_t length = 0;

    for (;;) {
        uint8_t byte = msh_getc(input);

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
            msh_write(" ", 1U);
        }
        msh_write(line + index, end - index);
        first = 0;
        index = end;
    }
    msh_write("\n", 1U);
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

    msh_puts("command not found: ");
    msh_write(line + command, command_end - command);
    msh_write("\n", 1U);
}

void minemu_user_main(void) {
    struct msh_input input = {.length = 0, .next = 0};
    char line[MSH_LINE_CAPACITY];

    for (;;) {
        msh_puts("msh> ");
        size_t length = msh_read_line(&input, line);
        msh_execute(line, length);
    }
}
