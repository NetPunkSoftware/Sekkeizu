#pragma once

#include <concurrentqueue.h>
#include <boost/pool/pool.hpp>


template <typename T>
class per_thread_pool
{
    using pool_t = boost::pool<>;

public:
    per_thread_pool() noexcept = default;

    template <typename... Args>
    T* get(Args&&... args) noexcept;

    void release(T* object) noexcept;

protected:
    inline auto& get_pool() noexcept
    {
        //auto& pool = np::this_fiber::template threadlocal<pool_t>(sizeof(T));
        thread_local pool_t pool(sizeof(T));
        return pool;
    }

private:
    moodycamel::ConcurrentQueue<void*> _free_objects;
};


template <typename T>
template <typename... Args>
T* per_thread_pool<T>::get(Args&&... args) noexcept
{
    void* ptr;
    if (!_free_objects.try_dequeue(ptr))
    {
        ptr = get_pool().malloc();
    }

    auto object = new (ptr) T(std::forward<Args>(args)...);
    return object;
}

template <typename T>
void per_thread_pool<T>::release(T* object) noexcept
{
    std::destroy_at(object);
    _free_objects.enqueue((void*)object);
}
