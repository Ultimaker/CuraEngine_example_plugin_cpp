#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <thread>

#include <agrpc/asio_grpc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <docopt/docopt.h> // Library for parsing command line arguments
#include <fmt/format.h> // Formatting library
#include <fmt/ranges.h> // Formatting library for ranges
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h> // Logging library

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

#include "cura/plugins/slots/simplify/v0/simplify.grpc.pb.h"
#include "cura/plugins/slots/simplify/v0/simplify.pb.h"
#include "cura/plugins/v0/broadcast_slots.pb.h"


struct plugin_metadata
{
    std::string plugin_name{ "UltiMaker basic simplification" };
    std::string slot_version{ "0.1.0-alpha.3" };
    std::string plugin_version{ "0.2.0-alpha.3" };
};

static plugin_metadata metadata{};


int main(int argc, const char** argv)
{
    spdlog::set_level(spdlog::level::debug);
    constexpr bool show_help = true;
    const std::map<std::string, docopt::value> args = docopt::docopt(fmt::format(plugin::cmdline::USAGE, plugin::cmdline::NAME), { argv + 1, argv + argc }, show_help, plugin::cmdline::VERSION_ID);

    std::unique_ptr<grpc::Server> server;

    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{ builder.AddCompletionQueue() };
    builder.AddListeningPort(fmt::format("{}:{}", args.at("<address>").asString(), args.at("<port>").asString()), grpc::InsecureServerCredentials());

    cura::plugins::slots::simplify::v0::SimplifyService::AsyncService service;
    builder.RegisterService(&service);
    grpc::AsyncGenericService generic_service;
    builder.RegisterAsyncGenericService(&generic_service);
    server = builder.BuildAndStart();

    std::unordered_map<std::string, std::string> settings;
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
                // Listen to the BroadcastSettingsRequest
                while (true)
                {
                    grpc::ServerContext broadcast_server_context;
                    cura::plugins::v0::BroadcastServiceSettingsRequest request;
                    grpc::ServerAsyncResponseWriter<google::protobuf::Empty> writer{ &broadcast_server_context };
                    co_await agrpc::request(&cura::plugins::slots::simplify::v0::SimplifyService::AsyncService::RequestBroadcastSettings, service, broadcast_server_context, request, writer, boost::asio::use_awaitable);
                    google::protobuf::Empty response;
                    co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);

                    for (const auto& [key, value] : request.global_settings().settings())
                    {
                        settings.emplace(key, value);
                        spdlog::info("Received setting: {} = {}", key, value);
                    }
                }
        }, boost::asio::detached);


    // Start the plugin main process
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            // Listen to the ModifyRequest
            while (true)
            {
                grpc::ServerContext server_context;
                server_context.AddInitialMetadata("cura-slot-version", metadata.slot_version);  // IMPORTANT: This NEEDS to be set!
                server_context.AddInitialMetadata("cura-plugin-name", metadata.plugin_name); // optional but recommended
                server_context.AddInitialMetadata("cura-plugin-version", metadata.plugin_version); // optional but recommended

                cura::plugins::slots::simplify::v0::SimplifyServiceModifyRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::slots::simplify::v0::SimplifyServiceModifyResponse> writer{ &server_context };
                co_await agrpc::request(&cura::plugins::slots::simplify::v0::SimplifyService::AsyncService::RequestModify, service, server_context, request, writer, boost::asio::use_awaitable);
                cura::plugins::slots::simplify::v0::SimplifyServiceModifyResponse response;

                grpc::Status status = grpc::Status::OK;
                try
                {
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
                }
                catch (const std::runtime_error& e)
                {
                    status = grpc::Status(grpc::StatusCode::INTERNAL, e.what());
                }
                catch (...)
                {
                    status = grpc::Status(grpc::StatusCode::INTERNAL, "Unknown error");
                }

                // spdlog::debug("Response: {}", request.DebugString());
                co_await agrpc::finish(writer, response, status, boost::asio::use_awaitable);
            }
        },
        boost::asio::detached);
    grpc_context.run();

    server->Shutdown();
}