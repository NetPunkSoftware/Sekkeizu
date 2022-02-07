#pragma once

#include "core/coreloop.hpp"

#include <palanteer.h>


template <typename derived>
class coreloop_palanteer_tick_time
{
public:
    template <typename T>
    void pre_tick(T* core_loop) noexcept;
    
    template <typename T>
    void post_tick(T* core_loop) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~coreloop_palanteer_tick_time() noexcept = default;
};


template <typename derived>
template <typename T>
void coreloop_palanteer_tick_time<derived>::pre_tick(T* core_loop) noexcept
{
    plBegin("Tick");
}

template <typename derived>
template <typename T>
void coreloop_palanteer_tick_time<derived>::post_tick(T* core_loop) noexcept
{
    plEnd("Tick");
}
