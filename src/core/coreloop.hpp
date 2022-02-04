#pragma once

#include "database/database.hpp"
#include "memory/per_thread_pool.hpp"

#include <pool/fiber_pool.hpp>
#include <synchronization/mutex.hpp>
#include <ext/executor.hpp>

#include <boost/asio.hpp>
#include <chrono>
#include <vector>

using udp = boost::asio::ip::udp;


struct secondary_pool_traits
{
    // Fiber pool traits
    static const bool preemtive_fiber_creation = true;
    static const uint32_t maximum_fibers = 20;
    static const uint16_t maximum_threads = 12;

    // Fiber traits
    static const uint32_t inplace_function_size = 64;
    static const uint32_t fiber_stack_size = 524288;
};

struct core_traits
{
    // Global server settings
    using clock_t = typename std::chrono::steady_clock;
    using base_time = std::chrono::milliseconds;
    static constexpr base_time heart_beat = base_time(50);
    
    // Fiber pools
    using core_pool_traits = np::detail::default_fiber_pool_traits;
    using database_pool_traits = secondary_pool_traits;
};


struct network_buffer
{
    uint16_t size;
    uint8_t data[500];
};


template <typename derived, typename traits=core_traits>
class core_loop
{
public:
    core_loop(uint16_t port, uint16_t core_threads, uint16_t network_threads, uint16_t database_threads) noexcept;

    template <typename database_traits>
    void start(database<database_traits>* database) noexcept;

protected:
    void handle_connections() noexcept;
    inline void release_network_buffer(network_buffer* buffer) noexcept;
    inline void release_network_endpoint(udp::endpoint* endpoint) noexcept;

private:
    // Fiber pools
    np::fiber_pool<typename traits::core_pool_traits> _core_pool;
    np::fiber_pool<typename traits::database_pool_traits> _database_pool;

    // Memory pools
    per_thread_pool<network_buffer> _data_mempool;
    per_thread_pool<udp::endpoint> _endpoints_mempool;

    // Server attributes
    bool _running;
    traits::clock_t::time_point _now;
    float _diff_mean;
    
    // Network objects
    std::vector<std::thread> _network_threads;
    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _work;
    udp::socket _socket;

    // Other
    uint16_t _num_core_threads;
    uint16_t _num_network_threads;
    uint16_t _num_database_threads;
};

template <typename derived, typename traits>
core_loop<derived, traits>::core_loop(uint16_t port, uint16_t num_core_threads, uint16_t num_network_threads, uint16_t num_database_threads) noexcept :
    _core_pool(),
    _database_pool(),
    _data_mempool(),
    _endpoints_mempool(),
    _running(false),
    _now(traits::clock_t::now()),
    _diff_mean(0),
    _network_threads(),
    _context(num_network_threads),
    _work(boost::asio::make_work_guard(_context)),
    _socket(_context, udp::endpoint(udp::v4(), port)),
    _num_core_threads(num_core_threads),
    _num_network_threads(num_network_threads),
    _num_database_threads(num_database_threads)
{}

template <typename derived, typename traits>
template <typename database_traits>
void core_loop<derived, traits>::start(database<database_traits>* database) noexcept
{
    // Fire up network thread
    for (int i = 0; i < _num_network_threads; ++i)
    {
        // TODO(gpascualg): The following should work: emplace_back(&boost::asio::io_context::run, & _context)
        _network_threads.emplace_back([this] { _context.run(); }); 
        handle_connections();
    }

    // Start database dedicated pool
    if (database != nullptr)
    {
        _database_pool.start(_num_database_threads, false);
        database->set_fiber_pool(_database_pool);
    }

    // Push main loop logic
    _running = true;
    _core_pool.push([this] () noexcept {
        while (_running)
        {
            // Save old tick and update time
            auto last_tick = _now;
            _now = traits::clock_t::now();

            // Compute time diff
            auto diff = std::chrono::duration_cast<traits::base_time>(_now - last_tick);
            _diff_mean = 0.95f * _diff_mean + 0.05f * diff.count();

            // Execute main tick
            reinterpret_cast<derived*>(this)->tick(diff);

            // TODO(gpascualg): Handle DB tasks if there is time

            // Sleep
            auto diff_mean = traits::base_time(static_cast<uint64_t>(std::ceil(_diff_mean)));
            auto update_time = std::chrono::duration_cast<traits::base_time>(traits::clock_t::now() - _now) + (diff_mean - traits::heart_beat);
            if (update_time < traits::heart_beat)
            {
                auto sleep_time = traits::heart_beat - update_time;
                
                // This is lost time which could be invested in other tasks
                std::this_thread::sleep_for(sleep_time);
            }
        }

        _core_pool.end();
    });

    // Start, wait until done, and then join
    _core_pool.start(_num_core_threads);
    _core_pool.join();

    // Stop networking
    _work.reset();
    _context.stop();
    for (auto& t : _network_threads)
    {
        t.join();
    }

}

template <typename derived, typename traits>
void core_loop<derived, traits>::handle_connections() noexcept
{
    // Get a new buffer
    auto buffer = _data_mempool.get();
    auto endpoint = _endpoints_mempool.get();

    _socket.async_receive_from(boost::asio::buffer(buffer->data, 500), *endpoint, 0, [this, buffer, endpoint](const auto& error, std::size_t bytes) noexcept {
        // std::cout << "Incoming packet from " << *endpoint << " [" << bytes << "b, " << static_cast<bool>(error) << "]" << std::endl;

        if (error)
        {
            _data_mempool.release(buffer);
            _endpoints_mempool.release(endpoint);
        }
        else
        {
            // Set read size
            buffer->size = bytes;
            
            // Let impl handle the packet
            reinterpret_cast<derived*>(this)->handle_network_packet(endpoint, buffer);
        }

        // Handle again
        handle_connections();
    });
}

template <typename derived, typename traits>
inline void core_loop<derived, traits>::release_network_buffer(network_buffer* buffer) noexcept
{
    _data_mempool.release(buffer);
}

template <typename derived, typename traits>
inline void core_loop<derived, traits>::release_network_endpoint(udp::endpoint* endpoint) noexcept
{
    _endpoints_mempool.release(endpoint);
}
