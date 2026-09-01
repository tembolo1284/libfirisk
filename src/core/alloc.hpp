#ifndef FIRISK_CORE_ALLOC_HPP_INCLUDED
#define FIRISK_CORE_ALLOC_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "firisk.h"
#include "compat.hpp"

namespace firisk {

struct allocator_table {
    void *(*f_malloc)(std::size_t);
    void *(*f_realloc)(void *, std::size_t);
    void  (*f_free)(void *);
};

/* defined in src/capi/firisk_alloc.cpp */
extern allocator_table g_allocators;
extern fir_panic_fn    g_panic_handler;

/* We do not propagate exceptions across the C ABI, and nothing in this
   library allocates unbounded amounts. So allocation failure is fatal
   rather than reported. The caller can install a handler with
   fir_set_panic_handler to log and abort on its own terms. */
[[noreturn]] void panic(const char *fmt, ...) noexcept FIR_PRINTF_FORMAT(1, 2);

[[noreturn]] void fatal_alloc_failure(std::size_t size) noexcept;

inline void *raw_malloc(std::size_t size) noexcept
{
    return g_allocators.f_malloc(size);
}

inline void *raw_realloc(void *ptr, std::size_t size) noexcept
{
    return g_allocators.f_realloc(ptr, size);
}

inline void raw_free(void *ptr) noexcept
{
    g_allocators.f_free(ptr);
}

inline void *checked_malloc(std::size_t size)
{
    void *ptr = g_allocators.f_malloc(size ? size : 1);
    if (!ptr)
        fatal_alloc_failure(size);
    return ptr;
}

inline void *checked_calloc(std::size_t count, std::size_t size)
{
    if (size && count > (SIZE_MAX / size))
        fatal_alloc_failure(SIZE_MAX);
    std::size_t total = count * size;
    void *ptr = checked_malloc(total);
    std::memset(ptr, 0, total);
    return ptr;
}

/* Over-aligned types would otherwise silently reach the *global*
   aligned operator new, bypassing the user's allocator entirely.
   The user only hands us malloc/realloc/free, so we over-allocate and
   stash the original pointer immediately below the aligned block. */
inline void *aligned_malloc(std::size_t size, std::size_t align)
{
    if (align < alignof(void *))
        align = alignof(void *);
    if (size > SIZE_MAX - align - sizeof(void *))
        fatal_alloc_failure(size);

    void *raw = g_allocators.f_malloc(size + align + sizeof(void *));
    if (!raw)
        fatal_alloc_failure(size);

    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void *);
    std::uintptr_t aligned =
        (base + align - 1) & ~static_cast<std::uintptr_t>(align - 1);

    reinterpret_cast<void **>(aligned)[-1] = raw;
    return reinterpret_cast<void *>(aligned);
}

inline void aligned_free(void *ptr) noexcept
{
    if (!ptr)
        return;
    g_allocators.f_free(reinterpret_cast<void **>(ptr)[-1]);
}

inline char *dup_cstring(const char *str)
{
    std::size_t length = std::strlen(str) + 1;
    char *rv = static_cast<char *>(checked_malloc(length));
    std::memcpy(rv, str, length);
    return rv;
}

} /* namespace firisk */

/* Place in the private section of every core class. Covers sized and
   aligned deletes too — omitting those lets the compiler pair our
   operator new with the global operator delete on some paths. */
#define FIR_IMPLEMENTS_ALLOCATORS                                             \
public:                                                                       \
    static void *operator new(std::size_t size)                               \
        { return ::firisk::checked_malloc(size); }                            \
    static void *operator new[](std::size_t size)                             \
        { return ::firisk::checked_malloc(size); }                            \
    static void operator delete(void *ptr) noexcept                           \
        { ::firisk::raw_free(ptr); }                                          \
    static void operator delete[](void *ptr) noexcept                         \
        { ::firisk::raw_free(ptr); }                                          \
    static void operator delete(void *ptr, std::size_t) noexcept              \
        { ::firisk::raw_free(ptr); }                                          \
    static void operator delete[](void *ptr, std::size_t) noexcept            \
        { ::firisk::raw_free(ptr); }                                          \
    static void *operator new(std::size_t size, std::align_val_t al)          \
        { return ::firisk::aligned_malloc(size,                               \
                    static_cast<std::size_t>(al)); }                          \
    static void *operator new[](std::size_t size, std::align_val_t al)        \
        { return ::firisk::aligned_malloc(size,                               \
                    static_cast<std::size_t>(al)); }                          \
    static void operator delete(void *ptr, std::align_val_t) noexcept         \
        { ::firisk::aligned_free(ptr); }                                      \
    static void operator delete[](void *ptr, std::align_val_t) noexcept       \
        { ::firisk::aligned_free(ptr); }                                      \
    static void operator delete(void *ptr, std::size_t,                       \
                                std::align_val_t) noexcept                    \
        { ::firisk::aligned_free(ptr); }                                      \
    static void operator delete[](void *ptr, std::size_t,                     \
                                  std::align_val_t) noexcept                  \
        { ::firisk::aligned_free(ptr); }                                      \
    static void *operator new(std::size_t, void *ptr) noexcept                \
        { return ptr; }                                                       \
    static void *operator new[](std::size_t, void *ptr) noexcept              \
        { return ptr; }                                                       \
    static void operator delete(void *, void *) noexcept {}                   \
    static void operator delete[](void *, void *) noexcept {}                 \
private:

    static_assert(true, "require a semicolon after FIR_IMPLEMENTS_ALLOCATORS")

#endif /* FIRISK_CORE_ALLOC_HPP_INCLUDED */
