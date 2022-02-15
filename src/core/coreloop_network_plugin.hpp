#pragma once

#include "core/coreloop.hpp"

#include <synchronization/mutex.hpp>

#include <boost/container_hash/hash.hpp>

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


template <typename derived, typename network_buffer>
class coreloop_network_plugin
{
protected:
    struct network_input_bundle
    {
        udp::endpoint* endpoint;
        network_buffer* buffer;
    };

public:
    template <typename T>
    void tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept;

    template <typename T>
    void handle_network_packet(T* core_loop, udp::endpoint* endpoint, network_buffer* buffer) noexcept;

    void disconnected(const udp::endpoint& endpoint) noexcept;

protected:
    // Do not destroy this class through base pointers
    ~coreloop_network_plugin() noexcept = default;

protected:
    // Pending new users
    std::unordered_set<udp::endpoint> _endpoints;
    std::unordered_set<udp::endpoint> _new_endpoints;

    // Pending network data
    np::mutex _pending_inputs_mutex;
    std::unordered_map<udp::endpoint, std::vector<network_buffer*>> _pending_inputs;
    np::counter _inputs_counter;
};


template <typename derived, typename network_buffer>
template <typename T>
void coreloop_network_plugin<derived, network_buffer>::tick(T* core_loop, const typename T::traits_t::base_time& diff) noexcept
{
    // First handle network packets
    if (_pending_inputs.size())
    {
        // Swap contents to new structures
        _pending_inputs_mutex.lock();

        decltype(_pending_inputs) pending_inputs;
        decltype(_new_endpoints) new_endpoints;

        std::swap(pending_inputs, _pending_inputs);
        std::swap(new_endpoints, _new_endpoints);

        _pending_inputs_mutex.unlock();

        // From begin to part, these are new
        for (auto& endpoint : new_endpoints)
        {
            // Add to our dictionary
            _endpoints.insert(endpoint);

            // Custom callback point
            reinterpret_cast<derived*>(this)->new_client(endpoint);
        }

        // Now push updates to each client and wait
        //  Note: We MUST wait, otherwise "pending_inputs" goes out of scope
        _inputs_counter.reset();
        for (auto& [endpoint, buffers] : pending_inputs)
        {
            core_loop->execute([this, &endpoint = endpoint, &buffers = buffers] {
                reinterpret_cast<derived*>(this)->client_inputs(endpoint, buffers);
            }, _inputs_counter);
        }
        _inputs_counter.wait();
    }

    // Now yield to user implementation
    reinterpret_cast<derived*>(this)->post_network_tick(diff);
}

template <typename derived, typename network_buffer>
template <typename T>
void coreloop_network_plugin<derived, network_buffer>::handle_network_packet(T* core_loop, udp::endpoint* endpoint, network_buffer* buffer) noexcept
{
    // TODO(gpascualg): Do we want this in a _network_pool, so that it doesn't hog the main pool's resources?
    core_loop->execute([this, core_loop, endpoint, buffer] {
        _pending_inputs_mutex.lock();

        // Is this a new user?
        if (_endpoints.find(*endpoint) == _endpoints.end())
        {
            _endpoints.insert(*endpoint);
            _new_endpoints.insert(*endpoint);
        }

        // Add to client pending inputs
        if (auto it = _pending_inputs.find(*endpoint); it != _pending_inputs.end())
        {
            it->second.push_back(buffer);
        }
        else
        {
            _pending_inputs.emplace(*endpoint, std::vector<network_buffer*>{ buffer });
        }

        _pending_inputs_mutex.unlock();

        // Endpoint can be already released
        core_loop->release_network_endpoint(endpoint);
    });
}

template <typename derived, typename network_buffer>
void coreloop_network_plugin<derived, network_buffer>::disconnected(const udp::endpoint& endpoint) noexcept
{
    _pending_inputs_mutex.lock();
    _endpoints.erase(_endpoints.find(endpoint));
    _pending_inputs_mutex.unlock();
}
