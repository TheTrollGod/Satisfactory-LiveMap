/* Compiles the vendored civetweb as part of the LiveMap module.
 * Options are set here, not in Build.cs, so they don't leak into C++ files. */
#define NO_SSL
#define NO_CGI
#define NO_CACHING
#define USE_WEBSOCKET
/* Atomic stop flag: the plain-int default is a data race on shutdown (found by TSan). */
#define STOP_FLAG_NEEDS_LOCK

/* Third-party code: don't let Unreal's warnings-as-errors (clang on Linux) or
 * MSVC /W4 fail the build on it. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include "../../../ThirdParty/civetweb/src/civetweb.c"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
