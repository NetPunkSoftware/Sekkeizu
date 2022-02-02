#pragma once

#include <pool/fiber_pool.hpp>

#include <concurrentqueue.h>
#include <boost/pool.hpp>


template <typename T>
class per_thread_pool
{
    using pool_t = boost::pool<>;

public:
    per_thread_pool() noexcept = default;

    template <typename... Args>
    T* get(Args&&... args) noexcept;

    void release(T* object) noexcept;

private:
    moodycamel::ConcurrentQueue<T*> _free_objects;
};


template <typename T>
template <typename... Args>
T* per_thread_pool<T>::get(Args&&... args)
{
    T* ptr;
    if (!_free_objects.try_dequeue(ptr))
    {
        auto& pool = this_fiber::template threadlocal<pool_t>(sizeof(T));
        ptr = pool.malloc();
    }

    auto object = new (ptr) T(std::forward<Args>(args)...);
    return object;
}

template <typename T>
void per_thread_pool<T>::release(T* object) noexcept
{
    std::destroy_at(object);
    _free_object.enqueue(object);
}
