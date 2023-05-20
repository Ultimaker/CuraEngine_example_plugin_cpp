#ifndef PLUGIN_PLUGIN_H
#define PLUGIN_PLUGIN_H

#include <agrpc/asio_grpc.hpp>
#include <agrpc/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "simplify/simplify.h" // Custom utilities for simplifying code

#include "plugin.grpc.pb.h"
#include "simplify.grpc.pb.h"


namespace plugin
{

template<class Service, auto AsyncService, class Response, class Request>
struct plugin_services
{
    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{ builder.AddCompletionQueue() };

    cura::plugins::proto::Plugin::AsyncService plugin_service;
    Service process_service;

    plugin_services(const std::string& address, uint32_t port)
    {
        builder.AddListeningPort(fmt::format("{}:{}", address, port), grpc::InsecureServerCredentials());
        builder.RegisterService(&plugin_service);
        builder.RegisterService(&process_service);
    }

    void operator()()
    {
        auto server = builder.BuildAndStart();
        boost::asio::co_spawn(grpc_context, handshake(), boost::asio::detached);
        boost::asio::co_spawn(grpc_context, process(), boost::asio::detached);
        grpc_context.run();
        server->Shutdown();
    }

    boost::asio::awaitable<void> handshake()
    {
        while (true)
        {
            grpc::ServerContext server_context;
            cura::plugins::proto::PluginRequest request;
            grpc::ServerAsyncResponseWriter<cura::plugins::proto::PluginResponse> writer{ &server_context };
            co_await agrpc::request(&cura::plugins::proto::Plugin::AsyncService::RequestIdentify, plugin_service, server_context, request, writer, boost::asio::use_awaitable);
            cura::plugins::proto::PluginResponse response;
            response.set_version("0.0.1");
            response.set_plugin_hash("qwerty-azerty-temp-hash");
            co_await agrpc::finish(writer, response, grpc::Status::OK, boost::asio::use_awaitable);
        }
    }


    boost::asio::awaitable<void> process()
    {
        while (true)
        {
            grpc::ServerContext server_context;
            Request request;
            grpc::ServerAsyncResponseWriter<Response> writer{ &server_context };

            co_await agrpc::request(AsyncService, process_service, server_context, request, writer, boost::asio::use_awaitable);
            // spdlog::debug("Request: {}", request.DebugString());
            Response response;

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
    }
};

} // namespace plugin

#endif // PLUGIN_PLUGIN_H
