#pragma once

#include "core/coreloop.hpp"


template <typename derived, typename base_time, uint64_t base_time_per_tick, uint8_t identifier>
class coreloop_scheduled_tick
{
public:
    template <typename T>
    void tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~coreloop_scheduled_tick() noexcept = default;

protected:
    base_time _elapsed = base_time(0);
};


template <typename derived, typename base_time, uint64_t base_time_per_tick, uint8_t identifier>
template <typename T>
void coreloop_scheduled_tick<derived, base_time, base_time_per_tick, identifier>::
    tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept
{
    _elapsed += diff;
    if (_elapsed > base_time(base_time_per_tick))
    {
        reinterpret_cast<derived*>(this)->template scheduled_tick<identifier>(_elapsed);
        _elapsed = base_time(0);
    }
}
