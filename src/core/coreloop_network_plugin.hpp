#pragma once

#include "core/coreloop.hpp"

#include <synchronization/mutex.hpp>

#include <boost/container_hash/hash.hpp>

#include <unordered_map>
#include <unordered_set>


namespace std
{
    template <>
    struct hash<udp::endpoint>
    {
        size_t operator()(udp::endpoint const& v) const noexcept {
            uint64_t seed = 0;
            boost::hash_combine(seed, v.address().to_v4().to_uint());
            boost::hash_combine(seed, v.port());
            return seed;
        }
    };
}


template <typename derived, typename network_buffer, uint8_t max_concurrent_threads>
class coreloop_network_plugin
{
protected:
    struct network_input_bundle
    {
        udp::endpoint* endpoint;
        network_buffer* buffer;
    };

public:
    constexpr coreloop_network_plugin() noexcept;

    template <typename T>
    void tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept;

    template <typename T>
    void handle_network_packet(T* core_loop, uint8_t unique_id, udp::endpoint* endpoint, network_buffer* buffer) noexcept;

    void disconnect(const udp::endpoint& endpoint) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~coreloop_network_plugin() noexcept = default;

protected:
    // Per network thread data
    std::array<np::mutex, max_concurrent_threads> _local_mutex;
    std::array<std::unordered_set<udp::endpoint>, max_concurrent_threads> _local_endpoints;
    std::array<std::unordered_map<udp::endpoint, std::vector<network_buffer*>>, max_concurrent_threads> _pending_inputs;

    // Shared data
    np::mutex _shared_mutex;
    std::unordered_set<udp::endpoint> _new_endpoints;
    std::unordered_set<udp::endpoint> _endpoints;
    std::unordered_map<udp::endpoint, std::vector<network_buffer*>> _endpoint_data;
    np::counter _inputs_counter;

    // Deletions
    std::vector<udp::endpoint> _pending_disconnects;
    np::mutex _disconnect_mutex;
};


template <typename derived, typename network_buffer, uint8_t max_concurrent_threads>
constexpr coreloop_network_plugin<derived, network_buffer, max_concurrent_threads>::coreloop_network_plugin() noexcept :
    _local_mutex(),
    _local_endpoints(),
    _pending_inputs(),
    _shared_mutex(),
    _new_endpoints(),
    _endpoints(),
    _endpoint_data(),
    _inputs_counter()
{}

template <typename derived, typename network_buffer, uint8_t max_concurrent_threads>
template <typename T>
void coreloop_network_plugin<derived, network_buffer, max_concurrent_threads>::tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept
{
    // Create any new client
    if (!_new_endpoints.empty())
    {
       _shared_mutex.lock();

        for (auto& endpoint : _new_endpoints)
        {
            // Add to endpoint data
            _endpoint_data.emplace(endpoint, std::vector<network_buffer*>{});

            // Custom callback point
            reinterpret_cast<derived*>(this)->new_client(endpoint);
        }

        _new_endpoints.clear();
        _shared_mutex.unlock();
    }

    // Accumulate any pending inputs
    for (uint8_t idx = 0; idx < max_concurrent_threads; ++idx)
    {
        // Lock local, add to local data
        _local_mutex[idx].lock();

        for (auto& [endpoint, pending_buffers] : _pending_inputs[idx])
        {
            auto it = _endpoint_data.find(endpoint); 
            assert(it != _endpoint_data.end() && "Client data should have been created already");
            for (auto& buffer : pending_buffers)
            {
                it->second.push_back(buffer);
            }
        }

        _pending_inputs[idx].clear();
        _local_mutex[idx].unlock();
    }

    // Reset inputs counter, we will acumulate from all threads
    _inputs_counter.reset();
    for (auto& [endpoint, buffers] : _endpoint_data)
    {
        core_loop->execute([this, &endpoint = endpoint, &buffers = buffers] {
            // Clear pending buffers after processing client
            reinterpret_cast<derived*>(this)->client_inputs(endpoint, buffers);
            buffers.clear();
        }, _inputs_counter);
    }
    _inputs_counter.wait();

    // Now yield to user implementation
    reinterpret_cast<derived*>(this)->post_network_tick(diff);

    // Execute any pending disconnect now
    if (!_pending_disconnects.empty())
    {
        _disconnect_mutex.lock();

        for (auto& endpoint : _pending_disconnects)
        {
            // Lock all locals
            for (uint8_t i = 0; i < max_concurrent_threads; ++i)
            {
                _local_mutex[i].lock();
            }

            // Lock shared, clear endpoint
            _shared_mutex.lock();
            _endpoints.erase(_endpoints.find(endpoint));
            _shared_mutex.unlock();

            // Clear locals, unlock
            for (uint8_t i = 0; i < max_concurrent_threads; ++i)
            {
                _local_endpoints[i].erase(_local_endpoints[i].find(endpoint));
                _local_mutex[i].unlock();
            }

            // Callback
            reinterpret_cast<derived*>(this)->on_disconnected(endpoint);
        }

        _disconnect_mutex.unlock();
    }
}

template <typename derived, typename network_buffer, uint8_t max_concurrent_threads>
template <typename T>
void coreloop_network_plugin<derived, network_buffer, max_concurrent_threads>::handle_network_packet(T* core_loop, uint8_t unique_id, udp::endpoint* endpoint, network_buffer* buffer) noexcept
{
    assert(unique_id < max_concurrent_threads && "Increase max_concurrent_threads in coreloop_network_plugin");

    // Check if we have this endpoint
    core_loop->execute([this, unique_id, core_loop, endpoint, buffer]() noexcept {
        auto& local_endpoints = _local_endpoints[unique_id];
        
        _local_mutex[unique_id].lock();
        if (local_endpoints.find(*endpoint) == local_endpoints.end())
        {
            _shared_mutex.lock();
            if (_endpoints.find(*endpoint) == _endpoints.end())
            {
                _endpoints.insert(*endpoint);
                _new_endpoints.insert(*endpoint);
            }
            _shared_mutex.unlock();
        }
     
        // Add to client pending inputs
        auto& pending_inputs = _pending_inputs[unique_id];
        if (auto it = pending_inputs.find(*endpoint); it != pending_inputs.end())
        {
            it->second.push_back(buffer);
        }
        else
        {
            pending_inputs.emplace(*endpoint, std::vector<network_buffer*>{ buffer });
        }

        _local_mutex[unique_id].unlock();

        // Endpoint can be already released
        core_loop->release_network_endpoint(endpoint);
    });
}

template <typename derived, typename network_buffer, uint8_t max_concurrent_threads>
void coreloop_network_plugin<derived, network_buffer, max_concurrent_threads>::disconnect(const udp::endpoint& endpoint) noexcept
{
    _disconnect_mutex.lock();
    _pending_disconnects.push_back(endpoint);
    _disconnect_mutex.unlock();
}

