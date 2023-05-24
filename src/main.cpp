#include <optional>
#include <thread>
#include <map>

#include <agrpc/asio_grpc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <docopt/docopt.h> // Library for parsing command line arguments
#include <fmt/format.h> // Formatting library
#include <fmt/ranges.h> // Formatting library for ranges
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h> // Logging library

#include "plugin/cmdline.h" // Custom command line argument definitions
#include "simplify/simplify.h" // Custom utilities for simplifying code

#include "cura/plugins/slots/simplify/v0/simplify.grpc.pb.h"
#include "cura/plugins/slots/simplify/v0/simplify.pb.h"

#define BUILD_ALLOW_REMOTE_CHANNELS 1
#if BUILD_ALLOW_REMOTE_CHANNELS
#include "../secrets/certificate.pem.h"
#include "../secrets/private_key.pem.h"
#endif

namespace buildopt
{
    constexpr bool ALLOW_REMOTE_CHANNELS = (BUILD_ALLOW_REMOTE_CHANNELS)==1;
} // namespace buildopt

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
    const auto host_str = args.at("<address>").asString();
    const auto port_str = args.at("<port>").asString();
    constexpr auto create_credentials =
        [&host_str]()
        {
            if (host_str == "localhost" || host_str == "127.0.0.1")
            {
                spdlog::info("Connect to engine via local-host.");
                return grpc::InsecureServerCredentials();
            }

            auto creds_config = grpc::SslServerCredentialsOptions();
            if (buildopt::ALLOW_REMOTE_CHANNELS)
            {
                // NOTE: In order to guarantee our users' security, remote plugins require a private key.
                //       You can either compile the engine (and/or Cura) so that it work with your own key-pair, or...
                //       ... if you want to run remote plugins against the precompiled Cura, please contact UltiMaker.
                grpc::SslServerCredentialsOptions::PemKeyCertPair key_pair = { std::string(secrets::private_key), std::string(secrets::certificate) };
                creds_config.pem_key_cert_pairs = { key_pair };
                creds_config.pem_root_certs = secrets::certificate;
            }
            spdlog::info("Credentials to connect remotely to an engine (at '{}').", host_str);
            return grpc::SslServerCredentials(creds_config);
        };
    spdlog::info("Listening on port '{}'.", port_str);
    builder.AddListeningPort(fmt::format("{}:{}", host_str, port_str), create_credentials());

    cura::plugins::slots::simplify::v0::SimplifyService::AsyncService service;
    builder.RegisterService(&service);
    server = builder.BuildAndStart();

    // Start the plugin main process
    boost::asio::co_spawn(
        grpc_context,
        [&]() -> boost::asio::awaitable<void>
        {
            spdlog::info("Started.");
            while (true)
            {
                spdlog::info("Ready for next.");

                grpc::ServerContext server_context;
                server_context.AddInitialMetadata("cura-slot-version", metadata.slot_version);  // IMPORTANT: This NEEDS to be set!
                server_context.AddInitialMetadata("cura-plugin-name", metadata.plugin_name); // optional but recommended
                server_context.AddInitialMetadata("cura-plugin-version", metadata.plugin_version); // optional but recommended

                cura::plugins::slots::simplify::v0::SimplifyServiceModifyRequest request;
                grpc::ServerAsyncResponseWriter<cura::plugins::slots::simplify::v0::SimplifyServiceModifyResponse> writer{ &server_context };
                co_await agrpc::request(&cura::plugins::slots::simplify::v0::SimplifyService::AsyncService::RequestModify, service, server_context, request, writer, boost::asio::use_awaitable);
                spdlog::info("Processing request.");
                cura::plugins::slots::simplify::v0::SimplifyServiceModifyResponse response;

                grpc::Status status = grpc::Status::OK;
                try
                {
                    Simplify simpl(request.max_deviation(), request.max_resolution(), request.max_area_deviation());
                    auto* rsp_polygons = response.mutable_polygons()->add_polygons();

                    for (const auto& polygon : request.polygons().polygons())
                    {
                        const auto& outline = polygon.outline();
                        geometry::polygon outline_poly;
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