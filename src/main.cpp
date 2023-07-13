#include <map>
#include <optional>
#include <thread>


#include <agrpc/asio_grpc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <docopt/docopt.h> // Library for parsing command line arguments
#include <fmt/format.h> // Formatting library
#include <fmt/ranges.h> // Formatting library for ranges
#include <google/protobuf/empty.pb.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h> // Logging library

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

#include "cura/plugins/slots/broadcast/v0/broadcast.grpc.pb.h"
#include "cura/plugins/slots/broadcast/v0/broadcast.pb.h"
#include "cura/plugins/slots/handshake/v0/handshake.grpc.pb.h"
#include "cura/plugins/slots/handshake/v0/handshake.pb.h"
#include "cura/plugins/slots/simplify/v0/simplify.grpc.pb.h"
#include "cura/plugins/slots/simplify/v0/simplify.pb.h"


struct plugin_metadata
{
    std::string plugin_name{ "UltiMaker basic simplification" };
    std::string slot_version{ "0.1.0-alpha.3" };
    std::string plugin_version{ "0.3.0-alpha.1" };
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
    builder.AddListeningPort(fmt::format("{}:{}", args.at("--address").asString(), args.at("--port").asString()), grpc::InsecureServerCredentials());

    cura::plugins::slots::handshake::v0::HandshakeService::AsyncService handshake_service;
    builder.RegisterService(&handshake_service);

    cura::plugins::slots::broadcast::v0::BroadcastService::AsyncService broadcast_service;
    builder.RegisterService(&broadcast_service);

    cura::plugins::slots::simplify::v0::SimplifyModifyService::AsyncService service;
    builder.RegisterService(&service);

    server = builder.BuildAndStart();

    // Start the handshake process
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            while (true)
            {
                grpc::ServerContext server_context;

                cura::plugins::slots::handshake::v0::CallRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::slots::handshake::v0::CallResponse> writer{ &server_context };
                co_await agrpc::request(&cura::plugins::slots::handshake::v0::HandshakeService::AsyncService::RequestCall, handshake_service, server_context, request, writer, boost::asio::use_awaitable);
                spdlog::info("Received handshake request");
                spdlog::info("Slot ID: {}, version_range: {}", static_cast<int>(request.slot_id()), request.version_range());

                cura::plugins::slots::handshake::v0::CallResponse response;
                response.set_plugin_name(metadata.plugin_name);
                response.set_plugin_version(metadata.plugin_version);
                response.set_slot_version(metadata.slot_version);
                response.set_plugin_version(metadata.plugin_version);
                response.mutable_broadcast_subscriptions()->Add("BroadcastSettings");

                co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);
            }
        },
        boost::asio::detached);

    // Listen to the Broadcast channel
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> settings;
    boost::asio::co_spawn(grpc_context,
                          [&]() -> boost::asio::awaitable<void>
                          {
                              while (true)
                              {
                                  grpc::ServerContext server_context;
                                  cura::plugins::slots::broadcast::v0::BroadcastServiceSettingsRequest request;
                                  grpc::ServerAsyncResponseWriter<google::protobuf::Empty> writer{ &server_context };
                                  co_await agrpc::request(&cura::plugins::slots::broadcast::v0::BroadcastService::AsyncService::RequestBroadcastSettings, broadcast_service, server_context, request, writer, boost::asio::use_awaitable);
                                  google::protobuf::Empty response{};
                                  co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);

                                  auto c_uuid = server_context.client_metadata().find("cura-engine-uuid");
                                  if (c_uuid == server_context.client_metadata().end()) {
                                      spdlog::warn("cura-engine-uuid not found in client metadata");
                                      continue;
                                  }
                                  std::string client_metadata = std::string { c_uuid->second.data(), c_uuid->second.size() };

                                  // We create a new settings map for this uuid
                                  std::unordered_map<std::string, std::string> uuid_settings;

                                  // We insert all the settings from the request to the uuid_settings map
                                  for (const auto& [key, value] : request.global_settings().settings())
                                  {
                                      uuid_settings.emplace(key, value);
                                      spdlog::info("Received setting: {} = {}", key, value);
                                  }

                                  // We save the settings for this uuid in the global settings map
                                  settings[client_metadata] = uuid_settings;
                              }
                          },
             boost::asio::detached);


    // Start the plugin modify process
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            while (true)
            {
                grpc::ServerContext server_context;
                cura::plugins::slots::simplify::v0::CallRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::slots::simplify::v0::CallResponse> writer{ &server_context };
                co_await agrpc::request(&cura::plugins::slots::simplify::v0::SimplifyModifyService::AsyncService::RequestCall, service, server_context, request, writer, boost::asio::use_awaitable);
                cura::plugins::slots::simplify::v0::CallResponse response;

                auto c_uuid = server_context.client_metadata().find("cura-engine-uuid");
                if (c_uuid == server_context.client_metadata().end()) {
                    spdlog::warn("cura-engine-uuid not found in client metadata");
                    continue;
                }
                std::string client_metadata = std::string { c_uuid->second.data(), c_uuid->second.size() };
                auto meshfix_maximum_resolution = static_cast<int>(std::stof(settings[client_metadata].at("meshfix_maximum_resolution")) * 1000);
                spdlog::info("meshfix_maximum_resolution: {}", meshfix_maximum_resolution);

                grpc::Status status = grpc::Status::OK;
                try
                {
                    Simplify simpl(request.max_deviation(), meshfix_maximum_resolution, request.max_area_deviation());
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