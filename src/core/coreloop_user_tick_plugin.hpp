#pragma once

#include "core/coreloop.hpp"


template <typename derived>
class coreloop_user_tick_plugin
{
public:
    template <typename T>
    void tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~coreloop_user_tick_plugin() noexcept = default;
};


template <typename derived>
template <typename T>
void coreloop_user_tick_plugin<derived>::tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept
{
    reinterpret_cast<derived*>(this)->user_tick(diff);
}
