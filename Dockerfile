# Stage 1: Flutter Builder
FROM debian:stable-slim AS web-builder
RUN apt-get update && apt-get install -y \
    curl git unzip xz-utils libglu1-mesa \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# Install Flutter
RUN git clone https://github.com/flutter/flutter.git -b stable /flutter
ENV PATH="/flutter/bin:$PATH"
RUN flutter doctor

COPY web_ui/pubspec.* web_ui/
RUN cd web_ui && flutter pub get

COPY web_ui/ web_ui/
RUN cd web_ui && flutter build web --release

# Stage 2: C++ Dependencies
FROM ubuntu:22.04 as base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git libgrpc++-dev libprotobuf-dev \
    protobuf-compiler-grpc libpqxx-dev libfmt-dev libspdlog-dev \
    libeigen3-dev nlohmann-json3-dev libcpp-httplib-dev uuid-dev libgtest-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Stage 3: C++ Builder
FROM base as builder
COPY CMakeLists.txt .
COPY proto/ proto/
COPY src/ src/
COPY tests/ tests/
RUN mkdir build && cd build && cmake .. && make -j$(nproc)

# Stage 4: Runtime
FROM ubuntu:22.04 as runtime
RUN apt-get update && apt-get install -y \
    libgrpc++1 libprotobuf23 libpqxx-6.4 libfmt8 libspdlog1 \
    python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Training dependencies
RUN pip3 install pandas scikit-learn

WORKDIR /app
COPY --from=builder /app/build/telemetry-generator .
COPY --from=builder /app/build/telemetry-api .
COPY --from=builder /app/build/telemetry-scorer .
COPY --from=builder /app/build/unit_tests .
COPY --from=web-builder /app/web_ui/build/web ./www
COPY python/ python/

EXPOSE 8080 50051
CMD ["./telemetry-api"]
