# Production Hardening Runbook

This document describes the production hardening features implemented for the Telemetry Systems Lab, including tuning, maintenance, and verification procedures.

## 1. PCA Training Batching

The PCA trainer now uses a memory-efficient batched iteration strategy instead of full result materialization.

### Tuning
You can tune the batch size using the following environment variable:
- `PCA_TRAIN_BATCH_SIZE`: Number of rows per batch (default: `10000`).

Example:
```bash
export PCA_TRAIN_BATCH_SIZE=50000
./build/telemetry-api
```

### Verification
- **Memory Footprint**: Monitor the API server memory during training. It should remain stable even for large datasets (1M+ rows), as only one batch is held in memory at a time.
- **Logs**: Look for "Processed N batches (M rows)" debug logs to confirm batched execution.
- **Metrics**: Check `train_rows_processed` and `train_duration_ms` in the `/metrics` endpoint or logs.

## 2. Dataset Deletion Consistency

When a dataset is explicitly deleted, all associated data is removed transactionally to prevent orphaned records.

### Cascade Delete Semantics
The `DeleteDatasetWithScores` procedure removes:
1.  All scores in `dataset_scores`.
2.  All background jobs in `dataset_score_jobs`.
3.  All telemetry in `host_telemetry_archival`.
4.  All alerts in `alerts`.
5.  All model runs in `model_runs`.
6.  The generation run metadata in `generation_runs`.

**Note**: This is an *explicit* deletion path. Retention-based cleanup (via `RunRetentionCleanup`) uses a different logic focused on time-based archival and may not remove all metadata if configured otherwise.

### Orphan Detection
The `GetScores` endpoint performs real-time orphan detection:
- It checks for scores that no longer have a corresponding record in the telemetry table.
- If found, it logs a warning: `Detected N orphaned scores for dataset X and model Y`.
- It emits a metric: `scores_orphan_count`.

## 3. API Route Registry

To prevent accidental route omissions or mismatches, the system maintains an authoritative `RouteSpec` registry.

### Maintenance
When adding a new API endpoint in `src/api_server.cpp`, you **must** also update `src/route_registry.cpp`:
1.  Add a `RouteSpec` entry with the correct method, pattern, and handler name.
2.  Update the expected count in `tests/unit/test_route_registry.cpp` if necessary.

### Verification
- **Startup Check**: The API server calls `ValidateRoutes()` on startup and will log a warning if the registry count is unexpected.
- **Local Route Probe**: You can run the route probing tool against a running server:
  ```bash
  export API_URL=http://localhost:8280
  ./build/api_route_tests
  ```
  This tool ensures every registered route responds with a status other than `404`.
