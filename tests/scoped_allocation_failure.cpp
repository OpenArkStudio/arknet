// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "scoped_allocation_failure.hpp"

namespace {

void before_allocation() {
    if (scoped_allocation_failure::armed)
    {
        scoped_allocation_failure::disarm();
        ++scoped_allocation_failure::injections;
        throw std::bad_alloc();
    }
}

void* allocate(std::size_t size) {
    before_allocation();
    if (auto* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc();
}

void* allocate_aligned(std::size_t size, std::align_val_t alignment) {
    before_allocation();
    const auto value = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    auto* pointer = _aligned_malloc(size ? size : 1, value);
#else
    void* pointer{};
    if (posix_memalign(&pointer, value < sizeof(void*) ? sizeof(void*) : value, size ? size : 1))
    {
        pointer = nullptr;
    }
#endif
    if (pointer)
    {
        return pointer;
    }
    throw std::bad_alloc();
}

void free_aligned(void* pointer) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return allocate(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, alignment);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try
    { return allocate_aligned(size, alignment); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    free_aligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    free_aligned(pointer);
}
