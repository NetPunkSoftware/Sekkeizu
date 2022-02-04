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

    // Network packet size
    static constexpr std::size_t packet_max_size = 500;
};

template <uint32_t packet_max_size>
struct network_buffer
{
    uint16_t size;
    uint8_t data[packet_max_size];
};


template <typename P, typename T, typename base_time>
concept plugin_has_tick = requires (P plugin, T* core, const base_time& diff)
{
    { plugin.tick(core, diff) };
};

template <typename P, typename T, typename Buff>
concept plugin_has_handle_network_packet = requires (P plugin, T* core, udp::endpoint* endpoint, Buff* buffer)
{
    { plugin.handle_network_packet(core, endpoint, buffer) };
};


template <typename traits, typename... plugins>
class core_loop : public plugins...
{
public:
    using traits_t = traits;

public:
    core_loop(uint16_t port, uint16_t core_threads, uint16_t network_threads, uint16_t database_threads) noexcept;

    template <typename database_traits>
    void start(database<database_traits>* database) noexcept;

    template <typename C>
    void send_data(const udp::endpoint& endpoint, const void* buffer, uint32_t size, C&& callback) noexcept;

    template <typename F>
    inline void execute(F&& function) noexcept;

    template <typename F>
    inline void execute(F&& function, np::counter& counter) noexcept;

    inline void release_network_buffer(network_buffer<traits::packet_max_size>* buffer) noexcept;
    inline void release_network_endpoint(udp::endpoint* endpoint) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~core_loop() noexcept = default;

    void handle_connections() noexcept;

    // NOTE(gpascualg): MSVC won't compile is directly calling plugins::tick, use this as a bypass
    inline void call_tick_proxy(const typename traits::base_time& diff) noexcept;
    inline void call_handle_network_packet_proxy(udp::endpoint* endpoint, network_buffer<traits::packet_max_size>* buffer) noexcept;

    // Per plugin call to check if the method is implemented in the plugin
    template <typename P>
    inline void call_tick_proxy_impl(const typename traits::base_time& diff) noexcept;
    
    template <typename P>
    inline void call_handle_network_packet_proxy_impl(udp::endpoint* endpoint, network_buffer<traits::packet_max_size>* buffer) noexcept;

protected:
    // Fiber pools
    np::fiber_pool<typename traits::core_pool_traits> _core_pool;
    np::fiber_pool<typename traits::database_pool_traits> _database_pool;

    // Memory pools
    per_thread_pool<network_buffer<traits::packet_max_size>> _data_mempool;
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

template <typename traits, typename... plugins>
core_loop<traits, plugins...>::core_loop(uint16_t port, uint16_t num_core_threads, uint16_t num_network_threads, uint16_t num_database_threads) noexcept :
    plugins()...,
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

template <typename traits, typename... plugins>
template <typename database_traits>
void core_loop<traits, plugins...>::start(database<database_traits>* database) noexcept
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
        database->set_fiber_pool(&_database_pool);
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

            // Execute plugins main ticks
            call_tick_proxy(diff);

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

template <typename traits, typename... plugins>
template <typename C>
void core_loop<traits, plugins...>::send_data(const udp::endpoint& endpoint, const void* buffer, uint32_t size, C&& callback) noexcept
{
    _socket.async_send_to(boost::asio::const_buffer(buffer, size), endpoint,
        [buffer, size, callback = std::forward<C>(callback)](const boost::system::error_code& error, std::size_t bytes) noexcept
    {
        callback(buffer, size, bytes);

        if (error)
        {
            // TODO(gpascualg): Do something in case of error
        }
    });
}

template <typename traits, typename... plugins>
template <typename F>
inline void core_loop<traits, plugins...>::execute(F&& function) noexcept
{
    _core_pool.push(std::forward<F>(function));
}

template <typename traits, typename... plugins>
template <typename F>
inline void core_loop<traits, plugins...>::execute(F&& function, np::counter& counter) noexcept
{
    _core_pool.push(std::forward<F>(function), counter);
}

template <typename traits, typename... plugins>
void core_loop<traits, plugins...>::handle_connections() noexcept
{
    // Get a new buffer
    auto buffer = _data_mempool.get();
    auto endpoint = _endpoints_mempool.get();

    _socket.async_receive_from(boost::asio::buffer(buffer->data, traits::packet_max_size), *endpoint, 0, [this, buffer, endpoint](const auto& error, std::size_t bytes) noexcept {
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
            
            // Let plugins handle the packet
            call_handle_network_packet_proxy(endpoint, buffer);
        }

        // Handle again
        handle_connections();
    });
}

template <typename traits, typename... plugins>
inline void core_loop<traits, plugins...>::release_network_buffer(network_buffer<traits::packet_max_size>* buffer) noexcept
{
    _data_mempool.release(buffer);
}

template <typename traits, typename... plugins>
inline void core_loop<traits, plugins...>::release_network_endpoint(udp::endpoint* endpoint) noexcept
{
    _endpoints_mempool.release(endpoint);
}

template <typename traits, typename... plugins>
inline void core_loop<traits, plugins...>::call_tick_proxy(const typename traits::base_time& diff) noexcept
{
    (..., call_tick_proxy_impl<plugins>(diff));
}

template <typename traits, typename... plugins>
inline void core_loop<traits, plugins...>::call_handle_network_packet_proxy(udp::endpoint* endpoint, network_buffer<traits::packet_max_size>* buffer) noexcept
{
    (..., call_handle_network_packet_proxy_impl<plugins>(endpoint, buffer));
}

template <typename traits, typename... plugins>
template <typename P>
inline void core_loop<traits, plugins...>::call_tick_proxy_impl(const typename traits::base_time& diff) noexcept
{
    if constexpr (plugin_has_tick<P, core_loop<traits, plugins...>, typename traits::base_time>)
    {
        this->P::tick(this, diff);
    }
}

template <typename traits, typename... plugins>
template <typename P>
inline void core_loop<traits, plugins...>::call_handle_network_packet_proxy_impl(udp::endpoint* endpoint, network_buffer<traits::packet_max_size>* buffer) noexcept
{
    if constexpr (plugin_has_handle_network_packet<P, core_loop<traits, plugins...>, network_buffer<traits::packet_max_size>>)
    {
        this->P::handle_network_packet(this, endpoint, buffer);
    }
}
