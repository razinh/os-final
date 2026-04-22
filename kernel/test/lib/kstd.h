// Host-build stub: replaces kernel/lib/kstd.h with standard C library headers.
// Placed ahead of kernel/lib/kstd.h via -iquote kernel/test in the test Makefile.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
