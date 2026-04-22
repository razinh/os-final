// // Kernel debug stubs for standalone test builds.
// // Must NOT include <stdio.h> / <cstdio> — that would conflict with debug.h's
// // "void puts(const char*)" declaration (stdio.h says "int puts(const char*)").

// #include "kernel/debug.h"
// #include <unistd.h>
// #include <string.h>
// #include <stdlib.h>

// void puts(const char* s) {
//     write(1, s, strlen(s));
// }

// void putch(const char c) {
//     write(1, &c, 1);
// }

// [[noreturn]] void shutdown(bool) { _exit(1); }

// [[noreturn]] void assert(const char* file, int line, const char* cond) {
//     write(2, "Kernel assert: ", 15);
//     write(2, cond, strlen(cond));
//     write(2, "\n", 1);
//     (void)file; (void)line;
//     _exit(1);
// }

// namespace impl {
//     void print_lock()   {}
//     void print_unlock() {}
// }
