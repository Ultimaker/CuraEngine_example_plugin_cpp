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
#include <docopt/docopt.h> // Library for parsing command line arguments
#include <fmt/format.h> // Formatting library
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <spdlog/spdlog.h> // Logging library

#include <optional>
#include <thread>

#include "plugin.grpc.pb.h"
#include "simplify.grpc.pb.h"

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

namespace asio = boost::asio;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-avoid-capturing-lambda-coroutines"
// begin-snippet: server-side-helloworld
// ---------------------------------------------------
// Server-side hello world which handles exactly one request from the client before shutting down.
// ---------------------------------------------------
// end-snippet
int main(int argc, const char** argv)
{
    spdlog::set_level(spdlog::level::debug);
    constexpr bool show_help = true;
    const std::map<std::string, docopt::value> args = docopt::docopt(fmt::format(plugin::cmdline::USAGE, plugin::cmdline::NAME), { argv + 1, argv + argc }, show_help, plugin::cmdline::VERSION_ID);

//    auto poly = geometry::polygon{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 } };
//
//    auto simpl = Simplify(100, 100, 100);
//    auto x = simpl.simplify(poly);

    //    std::unique_ptr<grpc::Server> plugin_server;
    //    std::unique_ptr<grpc::Server> simplify_server;

    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{ builder.AddCompletionQueue() };
    builder.AddListeningPort(fmt::format("{}:{}", args.at("<address>").asString(), args.at("<port>").asString()), grpc::InsecureServerCredentials());

    cura::plugins::proto::Plugin::AsyncService plugin_service;
    cura::plugins::proto::Simplify::AsyncService simplify_service;
    builder.RegisterService(&plugin_service);
    builder.RegisterService(&simplify_service);
    auto server = builder.BuildAndStart();

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void>
        {
            while (true)
            {
                grpc::ServerContext server_context;
                cura::plugins::proto::PluginRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::proto::PluginResponse> writer{ &server_context };
                co_await agrpc::request(&cura::plugins::proto::Plugin::AsyncService::RequestIdentify, plugin_service, server_context, request, writer, asio::use_awaitable);
                cura::plugins::proto::PluginResponse response;
                response.set_version("0.0.1");
                response.set_plugin_hash("qwerty-azerty-temp-hash");
                co_await agrpc::finish(writer, response, grpc::Status::OK, asio::use_awaitable);
            }
        },
        asio::detached);

    asio::co_spawn(
        grpc_context,
        [&]() -> asio::awaitable<void>
        {
            while (true)
            {
                grpc::ServerContext server_context;
                cura::plugins::proto::SimplifyRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::proto::SimplifyResponse> writer{ &server_context };

                co_await agrpc::request(&cura::plugins::proto::Simplify::AsyncService::RequestSimplify, simplify_service, server_context, request, writer, asio::use_awaitable);
//                spdlog::debug("Request: {}", request.DebugString());
                cura::plugins::proto::SimplifyResponse response;

                Simplify simpl(request.max_deviation(), request.max_resolution(), request.max_area_deviation());
                auto* rsp_polygons = response.mutable_polygons()->add_polygons();

                for (const auto& polygon : request.polygons().polygons())
                {
                    const auto& outline = polygon.outline();
                    geometry::polygon outline_poly{};
                    for (const auto& point : outline.path())
                    {
                        outline_poly.emplace_back(point.x(), point.y());
                    }
                    concepts::poly_range auto result_outline = simpl.simplify(outline_poly);

                    auto* rsp_outline = rsp_polygons->mutable_outline();
                    for (const auto& point : result_outline)
                    {
                        auto* rsp_outline_path = rsp_outline->add_path();
                        rsp_outline_path->set_x(point.X);
                        rsp_outline_path->set_y(point.Y);
                    }

                    auto* rsp_holes = rsp_polygons->mutable_holes();
                    for (const auto& hole : polygon.holes())
                    {
                        geometry::polygon holes_poly{};
                        for (const auto& point : hole.path())
                        {
                            holes_poly.emplace_back(point.x(), point.y());
                        }
                        concepts::poly_range auto holes_result = simpl.simplify(holes_poly);

                        auto* rsp_hole = rsp_polygons->mutable_holes()->Add();
                        for (const auto& point : holes_result)
                        {
                            auto* hole_path = rsp_hole->add_path();
                            hole_path->set_x(point.X);
                            hole_path->set_y(point.Y);
                        }
                    }
                }
//                spdlog::debug("Response: {}", request.DebugString());
                co_await agrpc::finish(writer, response, grpc::Status::OK, asio::use_awaitable);
            }
        },
        asio::detached);
    grpc_context.run();

    server->Shutdown();
}
#pragma clang diagnostic pop