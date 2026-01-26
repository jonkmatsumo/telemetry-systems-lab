cmake_minimum_required(VERSION 3.15)
project(telemetry-generator)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)



find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(nlohmann_json REQUIRED)


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
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
)
target_include_directories(telemetry_proto PUBLIC ${PROTO_GEN_DIR})

# Main Server
add_executable(telemetry-generator 
    src/main.cpp 
    src/server.cpp
    src/generator.cpp
    src/db_client.cpp
)

target_include_directories(telemetry-generator PRIVATE src)
target_link_libraries(telemetry-generator
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
    fmt::fmt
    spdlog::spdlog
    PkgConfig::PQXX
    PkgConfig::UUID
)

# Scorer Executable
add_executable(telemetry-scorer
    src/scorer_main.cpp
    src/db_client.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)

target_include_directories(telemetry-scorer PRIVATE src)
target_link_libraries(telemetry-scorer
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
    fmt::fmt
    spdlog::spdlog
    PkgConfig::PQXX
    PkgConfig::UUID
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
)

# HTTP API Service
add_executable(telemetry-api
    src/api_main.cpp
    src/api_server.cpp
    src/db_client.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)
target_include_directories(telemetry-api PRIVATE src)
target_link_libraries(telemetry-api
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
    fmt::fmt
    spdlog::spdlog
    PkgConfig::PQXX
    PkgConfig::UUID
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Benchmark Tool
add_executable(telemetry-benchmark
    src/benchmark_main.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)
target_include_directories(telemetry-benchmark PRIVATE src)
target_link_libraries(telemetry-benchmark
    fmt::fmt
    spdlog::spdlog
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
)

# Tests
enable_testing()
find_package(GTest REQUIRED)

# Unit Tests
add_executable(unit_tests 
    tests/unit/test_generator.cpp
    tests/unit/test_server.cpp
    tests/unit/test_contract.cpp
    tests/unit/test_preprocessing.cpp
    tests/unit/test_detector_a.cpp
    tests/unit/test_alert_manager.cpp
    src/generator.cpp
    src/db_client.cpp
    src/server.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
    tests/parity/test_parity_b.cpp
)

target_include_directories(unit_tests PRIVATE src tests)
target_link_libraries(unit_tests
    GTest::GTest
    GTest::Main
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
    fmt::fmt
    spdlog::spdlog
    PkgConfig::PQXX
    PkgConfig::UUID
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
)


add_test(NAME UnitTests COMMAND unit_tests)

# Integration Test Client (End-to-End)
add_executable(test_client tests/client.cpp)
target_link_libraries(test_client
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
)

# Integration Test (DB Only)
add_executable(db_integration_tests
    tests/integration/test_db_client.cpp
    src/db_client.cpp
)
target_include_directories(db_integration_tests PRIVATE src)
target_link_libraries(db_integration_tests
    GTest::GTest
    GTest::Main
    telemetry_proto
    PkgConfig::GRPC
    PkgConfig::PROTOBUF
    fmt::fmt
    spdlog::spdlog
    PkgConfig::PQXX
    PkgConfig::UUID
)


