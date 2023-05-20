#include <docopt/docopt.h> // Library for parsing command line arguments
#include <fmt/format.h> // Formatting library
#include <spdlog/spdlog.h> // Logging library

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "plugin/plugin.h"

namespace asio = boost::asio;
namespace proto = cura::plugins::proto;
using plugin_runner_t = plugin::plugin_services<proto::Simplify::AsyncService, &proto::Simplify::AsyncService::RequestSimplify, proto::SimplifyResponse, proto::SimplifyRequest>;


int main(int argc, const char** argv)
{
    spdlog::set_level(spdlog::level::debug);
    constexpr bool show_help = true;
    const std::map<std::string, docopt::value> args = docopt::docopt(fmt::format(plugin::cmdline::USAGE, plugin::cmdline::NAME), { argv + 1, argv + argc }, show_help, plugin::cmdline::VERSION_ID);

    plugin_runner_t plugin_runner{ args.at("<address>").asString(), static_cast<uint32_t>(args.at("<port>").asLong()) };
    plugin_runner();
}