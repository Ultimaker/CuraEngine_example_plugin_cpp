#ifndef PLUGIN_CMDLINE_H
#define PLUGIN_CMDLINE_H

#include <string>
#include <string_view>

#include <fmt/compile.h>

namespace plugin::cmdline
{

constexpr std::string_view NAME = "Simplify Boost Plugin";
constexpr std::string_view VERSION = "0.1.0";
static const auto VERSION_ID = fmt::format(FMT_COMPILE("{} {}"), NAME, VERSION);

constexpr std::string_view USAGE = R"({0}.

Usage:
  simplify_boost_plugin [--address=<address>] [--port=<port>]
  simplify_boost_plugin (-h | --help)
  simplify_boost_plugin --version

Options:
  -h --help                 Show this screen.
  --version                 Show version.
  -ip --address=<address>   The IP address to connect the socket to [default: localhost].
  -p --port=<port>          The port number to connect the socket to [default: 33700].
)";

} // namespace plugin::cmdline

#endif // PLUGIN_CMDLINE_H
