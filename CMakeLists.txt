cmake_minimum_required(VERSION 3.25)
project(simplify_boost_plugin)

find_package(Protobuf REQUIRED)
find_package(Boost REQUIRED)
find_package(spdlog REQUIRED)
find_package(docopt REQUIRED)
find_package(asio-grpc REQUIRED)


asio_grpc_protobuf_generate(
        GENERATE_GRPC GENERATE_MOCK_CODE
        OUT_VAR "ASIO_GRPC_PLUGIN_PROTO_SOURCES"
        OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
        IMPORT_DIRS "${CMAKE_CURRENT_LIST_DIR}"
        PROTOS "${CMAKE_CURRENT_LIST_DIR}/plugin.proto"
        )

function(asio_grpc_add_proto_target _asio_grpc_name)
    add_library(${_asio_grpc_name} OBJECT)

    target_sources(${_asio_grpc_name} PRIVATE ${ASIO_GRPC_PLUGIN_PROTO_SOURCES})

    target_link_libraries(${_asio_grpc_name} PUBLIC protobuf::libprotobuf asio-grpc-compile-options-cpp20)

    target_include_directories(${_asio_grpc_name} SYSTEM
            PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
endfunction()

add_executable(simplify_boost_plugin src/main.cpp include/simplify/types.h include/plugin/plugin.h)
asio_grpc_add_proto_target(asio-grpc-plugin-protos)

target_include_directories(simplify_boost_plugin
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )

target_link_libraries(simplify_boost_plugin PUBLIC protobuf::libprotobuf grpc::grpc asio-grpc::asio-grpc boost::boost spdlog::spdlog docopt_s asio-grpc-plugin-protos )

