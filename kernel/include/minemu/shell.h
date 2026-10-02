#ifndef MINEMU_SHELL_H
#define MINEMU_SHELL_H

/* Kernel-mode command loop. Prints msh> and does not return. */
void minemu_shell(void) __attribute__((noreturn));

#endif
