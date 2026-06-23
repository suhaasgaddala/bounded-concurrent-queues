#pragma once

#include <atomic>
#include <cstddef>
#include <new>

namespace orbitqueue::detail {

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t destructive_interference_size =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t destructive_interference_size = 64;
#endif

template <typename T>
struct alignas(destructive_interference_size) PaddedAtomic {
    std::atomic<T> value{};
};

template <typename T>
inline constexpr bool padded_atomic_layout_is_valid =
    alignof(PaddedAtomic<T>) >= destructive_interference_size &&
    sizeof(PaddedAtomic<T>) % destructive_interference_size == 0;

template <typename T>
consteval void verify_padded_atomic_layout() {
    static_assert(padded_atomic_layout_is_valid<T>);
}

} // namespace orbitqueue::detail
