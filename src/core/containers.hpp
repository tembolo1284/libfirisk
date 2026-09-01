#ifndef FIRISK_CORE_CONTAINERS_HPP_INCLUDED
#define FIRISK_CORE_CONTAINERS_HPP_INCLUDED

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alloc.hpp"
#include "proxy_allocator.hpp"

namespace firisk {

template <class T>
using Vector = std::vector<T, proxy_allocator<T>>;

using String = std::basic_string<char, std::char_traits<char>,
                                 proxy_allocator<char>>;

template <class K, class V, class Cmp = std::less<K>>
using Map = std::map<K, V, Cmp, proxy_allocator<std::pair<const K, V>>>;

/* std::make_unique calls the *global* operator new, which skips
   FIR_IMPLEMENTS_ALLOCATORS. Use make<T>() instead, everywhere. */
struct deleter {
    template <class T>
    void operator()(T *ptr) const noexcept { delete ptr; }
};

template <class T>
using UniquePtr = std::unique_ptr<T, deleter>;

template <class T, class... Args>
UniquePtr<T> make(Args &&...args)
{
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}

} /* namespace firisk */

#endif /* FIRISK_CORE_CONTAINERS_HPP_INCLUDED */
