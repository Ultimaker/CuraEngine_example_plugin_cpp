#ifndef PLUGIN_PLUGIN_H
#define PLUGIN_PLUGIN_H

#include <boost/asio/ip/tcp.hpp>


namespace plugin
{
namespace detail
{


struct plugin_fn
{

    boost::asio::ip::tcp::endpoint listen_endpoint{};
    boost::asio::ip::tcp::endpoint target_endpoint{};


    template <typename... Args>
    void operator()(Args&&... args) const
    {

    }

}

constexpr auto plugin = detail::plugin_fn{};

} // namespace plugin


}

#endif // PLUGIN_PLUGIN_H
