# Stage 1: Flutter Builder
FROM debian:stable-slim AS web-builder
RUN apt-get update && apt-get install -y \
    curl git unzip xz-utils libglu1-mesa \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m flutteruser
USER flutteruser
WORKDIR /home/flutteruser

# Install Flutter
RUN git clone https://github.com/flutter/flutter.git -b stable flutter
ENV PATH="/home/flutteruser/flutter/bin:$PATH"
RUN flutter doctor

COPY --chown=flutteruser:flutteruser web_ui/pubspec.* web_ui/
RUN cd web_ui && flutter pub get

COPY --chown=flutteruser:flutteruser web_ui/ web_ui/
RUN cd web_ui && flutter build web --release

# Stage 1b: Flutter Dev Environment
FROM web-builder AS dev-web
USER root
RUN apt-get update && apt-get install -y procps python3 && rm -rf /var/lib/apt/lists/*
USER flutteruser
CMD ["tail", "-f", "/dev/null"]

# Stage 2: C++ Dependencies
FROM ubuntu:22.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git libgrpc++-dev libprotobuf-dev \
    protobuf-compiler-grpc libpqxx-dev libfmt-dev libspdlog-dev \
    nlohmann-json3-dev libcpp-httplib-dev uuid-dev libgtest-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Stage 2b: Dev Image (for docker compose dev-up)
FROM base AS dev
CMD ["tail", "-f", "/dev/null"]

# Stage 3: C++ Builder
FROM base AS builder
COPY CMakeLists.txt .
COPY proto/ proto/
COPY src/ src/
COPY tests/ tests/
RUN mkdir build && cd build && cmake .. && make -j1 telemetry-generator telemetry-api telemetry-scorer telemetry-train-pca unit_tests

# Stage 4: Runtime
FROM ubuntu:22.04 AS runtime
RUN apt-get update && apt-get install -y \
    libgrpc++1 libprotobuf23 libpqxx-6.4 libfmt8 libspdlog1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/telemetry-generator .
COPY --from=builder /app/build/telemetry-api .
COPY --from=builder /app/build/telemetry-scorer .
COPY --from=builder /app/build/telemetry-train-pca .
COPY --from=builder /app/build/unit_tests .
COPY --from=web-builder /home/flutteruser/web_ui/build/web ./www

EXPOSE 8080 50051
CMD ["./telemetry-api"]
