#define PL_IMPLEMENTATION 1
#include <palanteer.h>

#include "core/coreloop_with_network_handler.hpp"
#include "database/database.hpp"


class core_loop_impl : public coreloop_with_network_handler<core_loop_impl>
{
    using base_t = coreloop_with_network_handler<core_loop_impl>;

public:
    core_loop_impl() :
        base_t(5454, 2, 2, 2)
    {}

    void new_client(const udp::endpoint& endpoint)
    {}

    void client_inputs(const udp::endpoint& endpoint, base_t::network_buffer* buffer)
    {}

    void post_network_tick(const std::chrono::milliseconds& diff)
    {}
};

int main()
{
    database<typename core_traits::database_pool_traits> db;

    core_loop_impl impl;
    impl.start(&db);

    return 0;
}
