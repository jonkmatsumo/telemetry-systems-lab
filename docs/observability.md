# Observability Contract (Training + Scoring)

This document defines the correlation identifiers, structured logging fields, error taxonomy, and metrics for the telemetry training + inference pipeline. It is intentionally minimal and stable to keep Flutter UI and C++ API contracts intact.

## Correlation IDs

All operations must propagate these identifiers end-to-end:

- request_id: Per HTTP request (from header `X-Request-ID` or generated UUID).
- model_run_id: Stable training run identifier (from `/train` response or DB record).
- dataset_id: Dataset run identifier (generation run_id).
- inference_run_id: Stable inference run identifier (from `/inference` response or DB record).
- score_job_id: Stable score job identifier (from `/jobs/score_dataset` response or DB record).

If a field does not apply, omit it rather than emitting empty strings.

## Structured Logs

Logs are single-line JSON with the following required fields (when applicable):

- ts: ISO8601 timestamp
- level: INFO | WARN | ERROR
- event: event name (e.g., http_request_start)
- component: api_server | db | trainer | model | generator
- request_id
- dataset_id
- model_run_id
- inference_run_id
- score_job_id
- route
- method
- status_code
- duration_ms
- rows
- artifact_path
- error_code
- error

### Standard Events

- http_request_start
- http_request_end
- http_request_error
- db_query
- db_insert
- train_start
- train_end
- train_error
- infer_start
- infer_end
- infer_error
- model_load_start
- model_load_end
- model_load_error
- artifact_write

## Error Code Taxonomy

Error responses use a stable `error_code` string. Log entries for failures must include the same `error_code`.

- E_HTTP_BAD_REQUEST
- E_HTTP_NOT_FOUND
- E_HTTP_INVALID_ARGUMENT
- E_HTTP_RESOURCE_EXHAUSTED
- E_HTTP_GRPC_ERROR
- E_DB_CONNECT_FAILED
- E_DB_QUERY_FAILED
- E_DB_INSERT_FAILED
- E_TRAIN_NO_DATA
- E_TRAIN_ARTIFACT_WRITE_FAILED
- E_MODEL_LOAD_FAILED
- E_INFER_SCORE_FAILED
- E_INTERNAL

## Metrics

Metrics use either the in-process registry (`/metrics`) or log-emitted metric events. Names and units are stable.

### Training

- train_duration_ms (histogram, ms)
- train_rows_processed (counter, rows)
- train_bytes_written (counter, bytes)
- train_db_query_duration_ms (histogram, ms)

### Inference

- infer_duration_ms (histogram, ms)
- infer_rows_scored (counter, rows)
- model_load_duration_ms (histogram, ms)
- model_bytes_read (counter, bytes)

### DB score persistence

- scores_insert_rows (counter, rows)
- scores_insert_duration_ms (histogram, ms)
- scores_query_duration_ms (histogram, ms)

## Response Contract

All JSON responses that originate from the API server must include `request_id`. Responses related to training or inference must include the relevant run identifiers:

- /train: model_run_id, request_id
- /train/{id}: request_id
- /inference: request_id, inference_run_id
- /jobs/score_dataset: request_id, score_job_id

## Debugging

To trace a request, filter logs by `request_id` and then pivot to run identifiers (model_run_id, inference_run_id, dataset_id).
