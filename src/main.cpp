#define PL_IMPLEMENTATION 1
#include <palanteer.h>

#include "core/coreloop_network_plugin.hpp"
#include "core/coreloop_user_tick_plugin.hpp"
#include "database/database.hpp"


class core_loop_impl : public core_loop<
        core_traits, 
        coreloop_network_plugin<core_loop_impl, core_traits::packet_max_size>,
        coreloop_user_tick_plugin<core_loop_impl>
    >
{
    using base_t = core_loop<
        core_traits,
        coreloop_network_plugin<core_loop_impl, core_traits::packet_max_size>,
        coreloop_user_tick_plugin<core_loop_impl>
    >;

public:
    core_loop_impl() :
        base_t(5454, 2, 2, 2)
    {}

    void new_client(const udp::endpoint& endpoint)
    {}

    void client_inputs(const udp::endpoint& endpoint, base_t::network_buffer* buffer)
    {}

    void post_network_tick(const typename core_traits::base_time& diff)
    {}

    void user_tick(const typename core_traits::base_time& diff)
    {}
};

int main()
{
    database<typename core_traits::database_pool_traits> db;

    core_loop_impl impl;
    impl.start(&db);

    return 0;
}
