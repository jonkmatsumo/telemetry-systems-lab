cmake_minimum_required(VERSION 3.15)
project(telemetry-generator)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Safety Nets: Warnings
set(COMMON_WARNINGS -Wall -Wextra -Wpedantic -Wunused -Wshadow -Wconversion -Wcast-align -Woverloaded-virtual)
add_compile_options(${COMMON_WARNINGS})

option(TREAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(TREAT_WARNINGS_AS_ERRORS OR DEFINED ENV{CI})
    add_compile_options(-Werror)
    message(STATUS "Treating warnings as errors (-Werror)")
endif()

# Sanitizers
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_CLANG_TIDY "Enable Clang-Tidy" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "Clang-Tidy enabled")
    else()
        message(WARNING "Clang-Tidy not found")
    endif()
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    message(STATUS "Address Sanitizer enabled")
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
    message(STATUS "Undefined Behavior Sanitizer enabled")
endif()

if(ENABLE_TSAN)
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        message(FATAL_ERROR "Thread Sanitizer cannot be combined with ASAN/UBSAN")
    endif()
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
    message(STATUS "Thread Sanitizer enabled")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)
pkg_search_module(HTTPLIB IMPORTED_TARGET cpp-httplib libcpp-httplib)

find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# Proto Generation
set(PROTO_SRC proto/telemetry.proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
find_program(PROTOC_EXECUTABLE protoc REQUIRED)
execute_process(COMMAND mkdir -p ${PROTO_GEN_DIR})

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
set_target_properties(telemetry_proto PROPERTIES CXX_CLANG_TIDY "")
target_link_libraries(telemetry_proto PkgConfig::GRPC PkgConfig::PROTOBUF)
target_include_directories(telemetry_proto PUBLIC ${PROTO_GEN_DIR})
target_compile_options(telemetry_proto PRIVATE -Wno-conversion -Wno-nullability-extension)

add_library(telemetry_linalg src/linalg/matrix.cpp)
target_include_directories(telemetry_linalg PUBLIC src)

add_library(telemetry_trainer
    src/training/pca_trainer.cpp
    src/training/telemetry_iterator.cpp
)
target_include_directories(telemetry_trainer PUBLIC src)
target_link_libraries(telemetry_trainer telemetry_linalg nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog PkgConfig::PQXX)

add_executable(telemetry-generator
    src/main.cpp
    src/server.cpp
    src/generator.cpp
    src/db_client.cpp
    src/db_connection_manager.cpp
    src/job_manager.cpp
)
target_include_directories(telemetry-generator PRIVATE src)
target_link_libraries(telemetry-generator telemetry_proto PkgConfig::GRPC PkgConfig::PROTOBUF fmt::fmt spdlog::spdlog PkgConfig::PQXX PkgConfig::UUID)

add_executable(telemetry-scorer
    src/scorer_main.cpp
    src/db_client.cpp
    src/db_connection_manager.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)
target_include_directories(telemetry-scorer PRIVATE src)
target_link_libraries(telemetry-scorer telemetry_proto telemetry_linalg telemetry_trainer PkgConfig::GRPC PkgConfig::PROTOBUF fmt::fmt spdlog::spdlog PkgConfig::PQXX PkgConfig::UUID nlohmann_json::nlohmann_json)

add_executable(telemetry-api
    src/api_main.cpp
    src/api_server.cpp
    src/route_registry.cpp
    src/db_client.cpp
    src/db_connection_manager.cpp
    src/pca_model_cache.cpp
    src/job_state_machine.cpp
    src/job_reconciler.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
    src/job_manager.cpp
)
target_include_directories(telemetry-api PRIVATE src)
target_link_libraries(telemetry-api telemetry_proto telemetry_linalg telemetry_trainer PkgConfig::GRPC PkgConfig::PROTOBUF fmt::fmt spdlog::spdlog PkgConfig::PQXX PkgConfig::UUID nlohmann_json::nlohmann_json Threads::Threads)
if(HTTPLIB_FOUND)
    target_link_libraries(telemetry-api PkgConfig::HTTPLIB)
endif()

add_executable(telemetry-benchmark
    src/benchmark_main.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)
target_include_directories(telemetry-benchmark PRIVATE src)
target_link_libraries(telemetry-benchmark telemetry_linalg fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json)

add_executable(telemetry-train-pca src/training/train_pca_main.cpp)
target_include_directories(telemetry-train-pca PRIVATE src)
target_link_libraries(telemetry-train-pca telemetry_trainer)

include(FetchContent)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

foreach(target gtest gtest_main gmock gmock_main)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "")
    endif()
endforeach()

enable_testing()
if(NOT TARGET GTest::GTest AND TARGET gtest)
    add_library(GTest::GTest ALIAS gtest)
endif()
if(NOT TARGET GTest::Main AND TARGET gtest_main)
    add_library(GTest::Main ALIAS gtest_main)
endif()
if(NOT TARGET GTest::gmock AND TARGET gmock)
    add_library(GTest::gmock ALIAS gmock)
endif()

add_executable(unit_tests
    tests/unit/test_generator.cpp
    tests/unit/test_server.cpp
    tests/unit/test_contract.cpp
    tests/unit/test_preprocessing.cpp
    tests/unit/test_detector_a.cpp
    tests/unit/test_alert_manager.cpp
    tests/unit/test_linalg.cpp
    tests/unit/test_pca_trainer.cpp
    tests/unit/test_telemetry_iterator.cpp
    tests/unit/test_route_registry.cpp
    tests/unit/test_api_debug.cpp
    tests/unit/test_api_response_meta.cpp
    tests/unit/test_pagination.cpp
    tests/unit/test_time_resolution.cpp
    tests/unit/test_db_client_security.cpp
    tests/unit/test_job_manager.cpp
    tests/unit/test_obs_metrics.cpp
    tests/unit/test_obs_context.cpp
    tests/unit/test_hardening_scaffolding.cpp
    tests/unit/test_api_scoring.cpp
    tests/unit/test_time_parsing.cpp
    tests/unit/test_db_connection_pool.cpp
    tests/unit/test_pca_model_cache.cpp
    tests/unit/test_job_state_machine.cpp
    tests/unit/test_job_reconciler.cpp
    tests/unit/test_generator_lifecycle.cpp
    tests/unit/test_api_safety.cpp
    tests/unit/test_error_classification.cpp
    tests/unit/test_api_performance.cpp
    src/api_server.cpp
    src/generator.cpp
    src/db_client.cpp
    src/db_connection_manager.cpp
    src/pca_model_cache.cpp
    src/job_state_machine.cpp
    src/job_reconciler.cpp
    src/route_registry.cpp
    src/server.cpp
    src/job_manager.cpp
    src/preprocessing.cpp
    src/detectors/detector_a.cpp
    src/detectors/pca_model.cpp
    src/alert_manager.cpp
)
target_include_directories(unit_tests PRIVATE src tests)
target_compile_definitions(unit_tests PRIVATE TELEMETRY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(unit_tests GTest::GTest GTest::Main $<$<TARGET_EXISTS:GTest::gmock>:GTest::gmock> $<$<NOT:$<TARGET_EXISTS:GTest::gmock>>:gmock> telemetry_proto telemetry_linalg telemetry_trainer PkgConfig::GRPC PkgConfig::PROTOBUF fmt::fmt spdlog::spdlog PkgConfig::PQXX PkgConfig::UUID nlohmann_json::nlohmann_json)
if(HTTPLIB_FOUND)
    target_link_libraries(unit_tests PkgConfig::HTTPLIB)
endif()
add_test(NAME UnitTests COMMAND unit_tests)
set_tests_properties(UnitTests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_client tests/client.cpp)
target_link_libraries(test_client telemetry_proto PkgConfig::GRPC PkgConfig::PROTOBUF)

add_executable(db_integration_tests tests/integration/test_db_client.cpp src/db_client.cpp src/db_connection_manager.cpp)
target_include_directories(db_integration_tests PRIVATE src)
target_link_libraries(db_integration_tests PRIVATE GTest::GTest GTest::Main telemetry_proto PkgConfig::GRPC PkgConfig::PROTOBUF fmt::fmt spdlog::spdlog PkgConfig::PQXX PkgConfig::UUID)

# Final Health Check target
add_executable(api_health_tests tests/integration/test_api_health.cpp)
target_link_libraries(api_health_tests PRIVATE GTest::GTest GTest::Main nlohmann_json::nlohmann_json)
if(HTTPLIB_FOUND)
    target_link_libraries(api_health_tests PRIVATE PkgConfig::HTTPLIB)
endif()
