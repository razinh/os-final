// Host-build stub: replaces kernel/print.h for test builds.
// Makes all kernel debug/print macros no-ops so the net stack compiles
// without needing kernel console output infrastructure.
#pragma once
#define KPRINT(str, ...)  ((void)0)
#define KPANIC(msg, ...)  ((void)0)
#define SAY(str, ...)     ((void)0)
#define KDEBUG(str, ...)  ((void)0)
#define ASSERT(cond)      ((void)0)
#define MISSING()         ((void)0)
