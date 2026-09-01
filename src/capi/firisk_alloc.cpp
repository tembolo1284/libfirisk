#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "firisk.h"
#include "../core/alloc.hpp"

namespace firisk {

allocator_table g_allocators = {
    std::malloc,
    std::realloc,
    std::free
};

fir_panic_fn g_panic_handler = nullptr;

void panic(const char *fmt, ...) noexcept
{
    char buf[256];

    std::va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (g_panic_handler) {
        g_panic_handler(buf);
        /* A handler that returns is a contract violation; we cannot
           continue, so fall through to abort regardless. */
    }

    std::fprintf(stderr, "firisk: %s\n", buf);
    std::abort();
}

void fatal_alloc_failure(std::size_t size) noexcept
{
    panic("allocation of %zu bytes failed", size);
}

} /* namespace firisk */

extern "C" {

void fir_set_allocators(void *(*f_malloc)(size_t),
                        void *(*f_realloc)(void *, size_t),
                        void (*f_free)(void *))
{
    if (!f_malloc || !f_realloc || !f_free)
        return;
    firisk::g_allocators.f_malloc  = f_malloc;
    firisk::g_allocators.f_realloc = f_realloc;
    firisk::g_allocators.f_free    = f_free;
}

void fir_set_panic_handler(fir_panic_fn handler)
{
    firisk::g_panic_handler = handler;
}

void *fir_malloc(size_t size)
{
    return firisk::raw_malloc(size);
}

void *fir_realloc(void *ptr, size_t size)
{
    return firisk::raw_realloc(ptr, size);
}

void fir_free(void *ptr)
{
    firisk::raw_free(ptr);
}

} /* extern "C" */
