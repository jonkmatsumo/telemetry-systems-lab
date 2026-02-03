# AGENTS.md

This repo is a full-stack telemetry system (C++ API + Flutter web UI). Use the
README as the source of truth for architecture, endpoints, and build steps.

## Key Context
- HTTP API (C++): `src/` (serves REST + static Flutter assets).
- Flutter web UI: `web_ui/`.
- DB schema/migrations: `db/`.
- gRPC/proto: `proto/`.

## Build/Run (local/dev)
- Build: `mkdir build && cd build && cmake .. && make -j4`
- Dev containers: `make infra-up` then `make dev-up`
- Web UI: http://localhost:8300

## API Surface (analytics)
- `GET /datasets/:id/summary`
- `GET /datasets/:id/topk`
- `GET /datasets/:id/timeseries`
- `GET /datasets/:id/histogram`

## Testing
- C++: `./build/unit_tests` (after build)
- Flutter: use `web_ui` tests (run as needed for changes)
