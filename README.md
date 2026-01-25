# Synthetic Telemetry Generator

A C++ gRPC service for generating synthetic telemetry data (CPU, Memory, Disk, Network) and persisting it to PostgreSQL.

## Architecture

- **Service**: C++20, gRPC, Protobuf
- **Storage**: PostgreSQL 16 (Timescale-compatible schema)
- **Deployment**: Single Docker container via Docker Compose

## Prerequisites

- Docker & Docker Compose
- `grpcurl` (optional, for manual API testing)

## Quick Start

## Quick Start

1. Start the environment (Infrastructure + App):
   ```bash
   make dev-up
   ```

2. The service listens on port `50051`. The database is on `5434`.

3. Rebuild incrementally during development:
   ```bash
   make build
   make run
   ```

4. To shell into the container for debugging:
   ```bash
   make dev-shell
   ```


## API Usage

### Generate Telemetry (Start Run)

```bash
grpcurl -plaintext -d '{
  "tier": "ALPHA",
  "host_count": 10,
  "start_time_iso": "2025-01-01T00:00:00Z",
  "end_time_iso": "2025-01-01T01:00:00Z",
  "interval_seconds": 60,
  "seed": 12345,
  "anomaly_config": {
    "point_rate": 0.01,
    "contextual_rate": 0.05
  }
}' localhost:50051 telemetry.TelemetryService/GenerateTelemetry
```

### Check Run Status

```bash
grpcurl -plaintext -d '{"run_id": "YOUR_RUN_ID"}' localhost:50051 telemetry.TelemetryService/GetRun
```

## Testing

### Unit Tests
Run unit tests inside the container:
```bash
docker exec telemetry_generator ./unit_tests
```

### Integration Test
Run the built-in test client which triggers a short generation run and verifies status:
```bash
docker exec telemetry_generator ./test_client
```

## Database Schema

- `generation_runs`: Metadata about each generation request.
- `host_telemetry_archival`: Time-series data points.

Connect to DB:
```bash
docker exec -it telemetry_postgres psql -U postgres -d telemetry
```