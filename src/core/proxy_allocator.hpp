#ifndef FIRISK_CORE_PROXY_ALLOCATOR_HPP_INCLUDED
#define FIRISK_CORE_PROXY_ALLOCATOR_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <new>

#include "alloc.hpp"

namespace firisk {

template <class T>
struct proxy_allocator {
    using value_type = T;

    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    proxy_allocator() noexcept = default;

    template <class U>
    proxy_allocator(const proxy_allocator<U> &) noexcept {}

    T *allocate(std::size_t n)
    {
        if (n > (SIZE_MAX / sizeof(T)))
            fatal_alloc_failure(SIZE_MAX);

        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            return static_cast<T *>(aligned_malloc(n * sizeof(T), alignof(T)));
        else
            return static_cast<T *>(checked_malloc(n * sizeof(T)));
    }

    void deallocate(T *p, std::size_t) noexcept
    {
        if constexpr (alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            aligned_free(p);
        else
            raw_free(p);
    }

    template <class U>
    friend bool operator==(const proxy_allocator &,
                           const proxy_allocator<U> &) noexcept
    {
        return true;
    }
};

} /* namespace firisk */

#endif /* FIRISK_CORE_PROXY_ALLOCATOR_HPP_INCLUDED */
