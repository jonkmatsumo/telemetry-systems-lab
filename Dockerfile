# Build Stage
FROM ubuntu:22.04 as builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
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
    && rm -rf /var/lib/apt/lists/*




WORKDIR /app

# Copy source
COPY CMakeLists.txt .
COPY proto/ proto/
COPY src/ src/
COPY tests/ tests/


# Build
RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Runtime Stage
FROM ubuntu:22.04

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



# Expose gRPC port
EXPOSE 50051

# Default command
CMD ["./telemetry-generator"]
