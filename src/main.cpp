#define PL_IMPLEMENTATION 1
#include <palanteer.h>

#include "core/coreloop.hpp"
#include "database/database.hpp"


class core_loop_impl : public core_loop<core_loop_impl>
{
public:
    core_loop_impl() :
        core_loop<core_loop_impl>(5454, 2, 2, 2)
    {}

    void tick(const std::chrono::milliseconds& diff)
    {}

    void handle_network_packet(udp::endpoint* endpoint, network_buffer* buffer)
    {}
};

int main()
{
    database<typename core_traits::database_pool_traits> db;

    core_loop_impl impl;
    impl.start(&db);

    return 0;
}
