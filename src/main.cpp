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

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

#include "simplify.grpc.pb.h"
#include "simplify.pb.h"

namespace asio = boost::asio;
namespace proto = cura::plugins::proto;


struct plugin_metadata
{
    cura::plugins::proto::SlotID slot_id{ cura::plugins::proto::SlotID::SIMPLIFY };
    std::string plugin_name{ "UltiMaker basic simplification" };
    std::string slot_version{ "0.1.0-alpha.1+grpc_tests" };
    std::string plugin_version{ "0.2.0-alpha.1+proto_tests" };
};

static plugin_metadata metadata{};

int main(int argc, const char** argv)
{
    spdlog::set_level(spdlog::level::debug);
    constexpr bool show_help = true;
    const std::map<std::string, docopt::value> args = docopt::docopt(fmt::format(plugin::cmdline::USAGE, plugin::cmdline::NAME), { argv + 1, argv + argc }, show_help, plugin::cmdline::VERSION_ID);

    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{ builder.AddCompletionQueue() };
    builder.AddListeningPort(fmt::format("{}:{}", args.at("<address>").asString(), args.at("<port>").asString()), grpc::InsecureServerCredentials());

    proto::Simplify::AsyncService service;
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();

    // Handshake with CuraEngine
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            grpc::ServerContext server_context;
            grpc::ServerAsyncResponseWriter<proto::PluginResponse> writer{ &server_context };
            while (true)
            {
                proto::PluginRequest request;
                co_await agrpc::request(&proto::Simplify::AsyncService::RequestIdentify, service, server_context, request, writer, asio::use_awaitable);
                spdlog::debug("Received an PluginRequest from CuraEngine {}", request.DebugString());
                proto::PluginResponse response;
                response.set_slot_id(metadata.slot_id);
                response.set_plugin_name(metadata.plugin_name);
                response.set_slot_version(metadata.slot_version);
                response.set_plugin_version(metadata.plugin_version);
                co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);
            }
        },
        boost::asio::detached);

    // Start the plugin main process
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            grpc::ServerContext server_context;
            grpc::ServerAsyncResponseWriter<proto::SimplifyResponse> writer{ &server_context };
            while (true)
            {
                proto::SimplifyRequest request;
                co_await agrpc::request(&proto::Simplify::AsyncService::RequestSimplify, service, server_context, request, writer, boost::asio::use_awaitable);
                // spdlog::debug("Request: {}", request.DebugString());
                proto::SimplifyResponse response;

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
                // spdlog::debug("Response: {}", request.DebugString());
                co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);
            }
        },
        boost::asio::detached);
    grpc_context.run();

    server->Shutdown();
}