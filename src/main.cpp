#include "core/coreloop.hpp"


class core_loop_impl : public core_loop<core_loop_impl>
{
public:
    core_loop_impl() :
        core_loop<core_loop_impl>(5454, 2, 2, 2)
    {}

    void tick(const std::chrono::duration& diff)
    {}

    void handle_network_packet(udp::endpoint* endpoint, network_buffer* buffer)
    {}
};

int main()
{
    return 0;
}
