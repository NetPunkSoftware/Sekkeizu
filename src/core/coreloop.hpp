#pragma once

#include <pool/fiber_pool.hpp>

#include <chrono>


struct core_loop_traits : public np::detail::default_fiber_pool_traits
{
    using clock_t = typename std::chrono::steady_clock;
    using base_time = std::chrono::milliseconds;

    static base_time heart_beat = base_time(50);
};

template <typename derived, typename traits=core_loop_traits>
class core_loop
{
public:
    void start(uint16_t number_of_threads);

private:
    np::fiber_pool<traits> _pool;

    traits::clock_t::timepoint _now;
    float _diff_mean;
};



void core_loop<derived, traits>::start(uint16_t number_of_threads)
{
    pool.push([this] {
         // Save old tick and update time
        auto last_tick = _now;
        _now = traits::clock_t::now();

        // Compute time diff
        auto diff = std::chrono::duration_cast<traits::base_time>(_now - last_tick);
        _diff_mean = 0.95f * _diff_mean + 0.05f * diff.count();

        // TODO(gpascualg): Handle network inputs

        // Execute main tick
        reinterpret_cast<derived*>(this)->tick(diff);

        // TODO(gpascualg): Handle DB tasks if there is time

        // Sleep
        auto diff_mean = traits::base_time(static_cast<uint64_t>(std::ceil(_diff_mean)));
        auto update_time = std::chrono::duration_cast<traits::base_time>(std_clock_t::now() - _now) + (diff_mean - traits::heart_beat);
        if (update_time < traits::heart_beat)
        {
            spdlog::trace("Sleep");
            auto sleep_time = traits::heart_beat - update_time;
            
            // This is lost time which could be invested in other tasks
            std::this_thread::sleep_for(sleep_time);
        }
        spdlog::trace(" > Done");
    });

    // Start, wait until done, and then join
    pool.start(number_of_threads);
    pool.join();
}
