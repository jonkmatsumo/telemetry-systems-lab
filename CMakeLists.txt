cmake_minimum_required(VERSION 3.15)
project(telemetry-generator)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(UUID REQUIRED uuid)


find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# Proto Generation
set(PROTO_SRC proto/telemetry.proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Find the protoc compiler
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# Generate C++ sources
execute_process(
    COMMAND mkdir -p ${PROTO_GEN_DIR}
)

add_custom_command(
    OUTPUT ${PROTO_GEN_DIR}/telemetry.grpc.pb.cc ${PROTO_GEN_DIR}/telemetry.grpc.pb.h
           ${PROTO_GEN_DIR}/telemetry.pb.cc ${PROTO_GEN_DIR}/telemetry.pb.h
    COMMAND ${PROTOC_EXECUTABLE}
        --grpc_out=${PROTO_GEN_DIR}
        --cpp_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`
        -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/telemetry.proto
    DEPENDS ${PROTO_SRC}
)

add_library(telemetry_proto
    ${PROTO_GEN_DIR}/telemetry.grpc.pb.cc
    ${PROTO_GEN_DIR}/telemetry.grpc.pb.h
    ${PROTO_GEN_DIR}/telemetry.pb.cc
    ${PROTO_GEN_DIR}/telemetry.pb.h
)

target_link_libraries(telemetry_proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)
target_include_directories(telemetry_proto PUBLIC ${PROTO_GEN_DIR})

# Main Server
add_executable(telemetry-generator 
    src/main.cpp 
    src/server.cpp
    src/generator.cpp
    src/db_client.cpp
)

target_include_directories(telemetry-generator PRIVATE ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS} src)
target_link_libraries(telemetry-generator
    telemetry_proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    fmt::fmt
    spdlog::spdlog
    ${PQXX_LIBRARIES}
    ${UUID_LIBRARIES}
)

# Tests
enable_testing()
find_package(GTest REQUIRED)

# Unit Tests
add_executable(unit_tests 
    tests/unit/test_generator.cpp
    tests/unit/test_server.cpp
    src/generator.cpp
    src/db_client.cpp
    src/server.cpp
)

target_include_directories(unit_tests PRIVATE src tests ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})
target_link_libraries(unit_tests
    GTest::GTest
    GTest::Main
    telemetry_proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    fmt::fmt
    spdlog::spdlog
    ${PQXX_LIBRARIES}
    ${UUID_LIBRARIES}
)

add_test(NAME UnitTests COMMAND unit_tests)

# Integration Test Client (End-to-End)
add_executable(test_client tests/client.cpp)
target_link_libraries(test_client
    telemetry_proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

# Integration Test (DB Only)
add_executable(db_integration_tests
    tests/integration/test_db_client.cpp
    src/db_client.cpp
)
target_include_directories(db_integration_tests PRIVATE src ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})
target_link_libraries(db_integration_tests
    GTest::GTest
    GTest::Main
    telemetry_proto
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    fmt::fmt
    spdlog::spdlog
    ${PQXX_LIBRARIES}
    ${UUID_LIBRARIES}
)


