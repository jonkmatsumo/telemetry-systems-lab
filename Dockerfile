# Base Stage: dependencies
FROM ubuntu:22.04 as base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    libpqxx-dev \
    libfmt-dev \
    libspdlog-dev \
    uuid-dev \
    libgtest-dev \
    pkg-config \
    gdb \
    gdbserver \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Dev Stage: for incremental builds and debugging
# Source code is NOT copied here; it will be bind mounted
# Build artifacts will be persisted in a volume
FROM base as dev
CMD ["tail", "-f", "/dev/null"]

# Builder Stage: for creating production binaries
FROM base as builder
COPY CMakeLists.txt .
COPY proto/ proto/
COPY src/ src/
COPY tests/ tests/

RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Runtime Stage: minimal production image
FROM ubuntu:22.04 as runtime

RUN apt-get update && apt-get install -y \
    libgrpc++1 \
    libprotobuf23 \
    libpqxx-6.4 \
    libfmt8 \
    libspdlog1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/telemetry-generator .
COPY --from=builder /app/build/unit_tests .
COPY --from=builder /app/build/test_client .
COPY --from=builder /app/build/db_integration_tests .

EXPOSE 50051
CMD ["./telemetry-generator"]
