cmake_minimum_required(VERSION 3.15)
project(telemetry-generator)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# Proto functionality will be added in Phase B
# For now, just a dummy executable to pass the docker build
add_executable(telemetry-generator src/main.cpp)

target_include_directories(telemetry-generator PRIVATE ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})
target_link_libraries(telemetry-generator
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    fmt::fmt
    spdlog::spdlog
)
