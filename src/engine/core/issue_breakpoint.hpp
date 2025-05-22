#pragma once

#if _WIN32
#define SAH_BREAKPOINT __debugbreak()
#else
#include <signal.h>
#define SAH_BREAKPOINT raise(SIGINT)
#endif
