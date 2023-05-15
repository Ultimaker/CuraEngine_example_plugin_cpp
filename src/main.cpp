// Copyright 2022 Dennis Hezel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "plugin.grpc.pb.h"

#include <agrpc/asio_grpc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <docopt/docopt.h> // Library for parsing command line arguments
#include <spdlog/spdlog.h> // Logging library
#include <fmt/format.h> // Formatting library

#include <optional>
#include <thread>

#include "plugin.pb.h"
#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

namespace asio = boost::asio;

// begin-snippet: server-side-helloworld
// ---------------------------------------------------
// Server-side hello world which handles exactly one request from the client before shutting down.
// ---------------------------------------------------
// end-snippet
int main(int argc, const char** argv)
{
    constexpr bool show_help = true;
    const std::map<std::string, docopt::value> args = docopt::docopt(fmt::format(plugin::cmdline::USAGE, plugin::cmdline::NAME), { argv + 1, argv + argc }, show_help, plugin::cmdline::VERSION_ID);

    std::unique_ptr<grpc::Server> server;

    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{builder.AddCompletionQueue()};
    builder.AddListeningPort(fmt::format("{}:{}", args.at("<address>").asString(), args.at("<port>").asString()), grpc::InsecureServerCredentials());

    cura::plugins::proto::Plugin::AsyncService service;
    builder.RegisterService(&service);
    server = builder.BuildAndStart();

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void>
        {
            grpc::ServerContext server_context;
            cura::plugins::proto::PluginRequest request;
            grpc::ServerAsyncResponseWriter<cura::plugins::proto::PluginResponse> writer{&server_context};
            co_await agrpc::request(&cura::plugins::proto::Plugin::AsyncService::RequestIdentify, service, server_context,
                                    request, writer, asio::use_awaitable);
            cura::plugins::proto::PluginResponse response;
            response.set_version("0.0.1");
            response.set_plugin_hash("qwerty-azerty-temp-hash");
            co_await agrpc::finish(writer, response, grpc::Status::OK, asio::use_awaitable);
        },
        asio::detached);

    grpc_context.run();

    server->Shutdown();
}