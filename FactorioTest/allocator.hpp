#pragma once
// vibe coded

#include <cstdlib>
#include <new>
#include <limits>
#include <memory>
#include <type_traits>

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif

template<typename T>
class HugePageAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind { using other = HugePageAllocator<U>; };

    HugePageAllocator() noexcept = default;
    template<typename U> HugePageAllocator(const HugePageAllocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        // compute requested bytes
        const std::size_t bytes = n * sizeof(T);
        return static_cast<pointer>(allocate_bytes(bytes));
    }

    void deallocate(pointer p, size_type /*n*/) noexcept {
        free_bytes(static_cast<void*>(p));
    }

    // object construction helpers (C++17: not required by allocator concept but useful)
    template<class U, class... Args>
    void construct(U* p, Args&&... args) { ::new ((void*)p) U(std::forward<Args>(args)...); }

    template<class U>
    void destroy(U* p) { p->~U(); }

    size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    // Allocators are stateless -> equality always true
    bool operator==(const HugePageAllocator&) const noexcept { return true; }
    bool operator!=(const HugePageAllocator& a) const noexcept { return !operator==(a); }

private:
    static constexpr std::size_t ALIGNMENT = 2 * 1024 * 1024; // 2 MiB
    static_assert((ALIGNMENT& (ALIGNMENT - 1)) == 0, "ALIGNMENT must be power of two");

    static void* allocate_bytes(std::size_t bytes) {
        if (bytes == 0) return nullptr;

        // POSIX path: prefer posix_memalign (doesn't require size multiple)
#if defined(_WIN32) || defined(_WIN64)
        void* p = _aligned_malloc(bytes, ALIGNMENT);
        if (!p) throw std::bad_alloc();
        return p;
#else
        void* p = nullptr;
        int res = posix_memalign(&p, ALIGNMENT, bytes);
        if (res != 0) {
            // posix_memalign returns error code (EINVAL, ENOMEM)
            throw std::bad_alloc();
        }
        return p;
#endif
    }

    static void free_bytes(void* p) noexcept {
        if (!p) return;
#if defined(_WIN32) || defined(_WIN64)
        _aligned_free(p);
#else
        free(p);
#endif
    }
};