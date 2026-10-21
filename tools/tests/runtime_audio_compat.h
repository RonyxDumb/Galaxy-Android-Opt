#pragma once
#include <cmath>

// glibc declares private __fabs* symbols that bionic does not. Keep those
// declarations separate from the Wii intrinsic shims when building on Linux.
#define __fabsf runtime_test_fabsf
#define __fabs runtime_test_fabs
// The Android CMake target gets this declaration from forced port/compat.h.
extern "C" int port_skip_fast_forwarding(void);
