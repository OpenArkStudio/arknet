// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <arknet/config.hpp>
#include <arknet/base/detail/type_traits.hpp>
namespace arknet::detail
{
template<class ThreadLocal> constexpr std::size_t calc_allocator_storage_size() noexcept
{
#ifdef ARKNET_ALLOCATOR_STORAGE_SIZE
    return ARKNET_ALLOCATOR_STORAGE_SIZE;
#else
    return ThreadLocal::value ? 768 : 1024;
#endif
}
template<std::size_t Size> struct allocator_size_op { static constexpr std::size_t size = Size; };
struct allocator_fixed_size_tag {};
template<std::size_t Size> struct allocator_fixed_size_op : allocator_fixed_size_tag { static constexpr std::size_t size = Size; };
template<class Args> struct allocator_size_traits
{
    static constexpr std::size_t value = [] { if constexpr (requires { Args::allocator_storage_size; }) return std::size_t(Args::allocator_storage_size); else return std::size_t(0); }();
};
template<class Args> using assizer = allocator_size_op<allocator_size_traits<Args>::value>;
template<class ThreadLocal, class Size> constexpr std::size_t get_allocator_storage_size() noexcept
{
    if constexpr (std::is_base_of_v<allocator_fixed_size_tag, Size>) return Size::size;
#ifdef ARKNET_ALLOCATOR_STORAGE_SIZE
    else return ARKNET_ALLOCATOR_STORAGE_SIZE;
#else
    else if constexpr (Size::size >= 64 && Size::size <= 1024 * 1024) return Size::size;
    else return calc_allocator_storage_size<ThreadLocal>();
#endif
}
template<class ThreadLocal = std::true_type, class Size = allocator_size_op<calc_allocator_storage_size<ThreadLocal>()>>
class handler_memory
{
public:
    static constexpr std::size_t storage_size = get_allocator_storage_size<ThreadLocal, Size>();
    handler_memory() = default;
    handler_memory(const handler_memory&) = delete;
    handler_memory& operator=(const handler_memory&) = delete;
    void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
    {
        if (bytes <= storage_size && alignment <= alignof(std::max_align_t) && !occupied_.test_and_set(std::memory_order_acquire)) return storage_.data();
        if (alignment > alignof(std::max_align_t)) return ::operator new(bytes, std::align_val_t(alignment));
        return ::operator new(bytes);
    }
    void deallocate(void* pointer, std::size_t alignment = alignof(std::max_align_t)) noexcept
    {
        if (pointer == storage_.data()) occupied_.clear(std::memory_order_release);
        else if (alignment > alignof(std::max_align_t)) ::operator delete(pointer, std::align_val_t(alignment));
        else ::operator delete(pointer);
    }
private:
    alignas(std::max_align_t) std::array<std::byte, storage_size> storage_{};
    std::atomic_flag occupied_ = ATOMIC_FLAG_INIT;
};
template<class T, class ThreadLocal, class Size> class handler_allocator
{
    template<class, class, class> friend class handler_allocator;
public:
    using value_type = T;
    template<class U> struct rebind { using other = handler_allocator<U, ThreadLocal, Size>; };
    explicit handler_allocator(handler_memory<ThreadLocal, Size>& memory) noexcept : memory_(&memory) {}
    template<class U> handler_allocator(const handler_allocator<U, ThreadLocal, Size>& other) noexcept : memory_(other.memory_) {}
    template<class U> bool operator==(const handler_allocator<U, ThreadLocal, Size>& other) const noexcept { return memory_ == other.memory_; }
    T* allocate(std::size_t count) const
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) throw std::bad_array_new_length();
        return static_cast<T*>(memory_->allocate(count * sizeof(T), alignof(T)));
    }
    void deallocate(T* pointer, std::size_t) const noexcept { memory_->deallocate(pointer, alignof(T)); }
private:
    handler_memory<ThreadLocal, Size>* memory_;
};
template<class Handler, class ThreadLocal, class Size> class custom_alloc_handler
{
public:
    using allocator_type = handler_allocator<Handler, ThreadLocal, Size>;
    custom_alloc_handler(handler_memory<ThreadLocal, Size>& memory, Handler handler) : memory_(&memory), handler_(std::move(handler)) {}
    allocator_type get_allocator() const noexcept { return allocator_type(*memory_); }
    template<class... Args> decltype(auto) operator()(Args&&... args) { return handler_(std::forward<Args>(args)...); }
private:
    handler_memory<ThreadLocal, Size>* memory_;
    Handler handler_;
};
template<class Handler, class ThreadLocal, class Size> auto make_allocator(handler_memory<ThreadLocal, Size>& memory, Handler&& handler)
{
    return custom_alloc_handler<std::decay_t<Handler>, ThreadLocal, Size>(memory, std::forward<Handler>(handler));
}
}
