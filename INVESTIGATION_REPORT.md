# Telemetry Project: Investigation Report

**Branch:** `investigate/telemetry-data-path-failures`
**Date:** 2026-02-01
**Investigator:** Claude (Senior Systems Engineer)

---

## Executive Summary

This investigation identified **four interconnected failures** in the telemetry system. The root causes are:

1. **Missing API Route Registration** - Critical routes for training and model listing are defined but never wired up
2. **Incorrect pqxx Stream API Usage** - Training uses `stream_from(txn, query)` which treats the query as a table name
3. **Broken Foreign Key Constraint** - `dataset_scores.record_id` references a partitioned table incorrectly
4. **Cascading Schema Load Failure** - The metrics schema endpoint works but errors are caused by downstream model list failures

---

## Phase 0: System Entry Points & Data Flow Map

### Architecture Overview

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Flutter UI    │────>│  C++ API Server  │────>│   PostgreSQL    │
│   (web_ui/)     │     │  (api_server.cpp)│     │  (Partitioned)  │
└─────────────────┘     └──────────────────┘     └─────────────────┘
                                │
                                ▼
                        ┌──────────────────┐
                        │  gRPC Generator  │
                        │  (Port 52051)    │
                        └──────────────────┘
```

### Key Entry Points

| Flow | UI Entry | API Route | Backend Handler | DB Tables |
|------|----------|-----------|-----------------|-----------|
| Dataset Selection | `control_panel.dart:44` | `GET /datasets` | `HandleListDatasets()` | `generation_runs` |
| Sample Fetch | `control_panel.dart:148` | `GET /datasets/{id}/samples` | `HandleGetDatasetSamples()` | `host_telemetry_archival` |
| Metrics Schema | `dataset_analytics_screen.dart:49` | `GET /schema/metrics` | Lambda at line 183 | N/A (hardcoded) |
| Model Training | `control_panel.dart:262` | `POST /train` | `HandleTrainModel()` | `model_runs` |
| Model List | `models_screen.dart:31` | `GET /models` | `HandleListModels()` | `model_runs` |
| Inference | `control_panel.dart:297` | `POST /inference` | `HandleInference()` | `inference_runs` |

### Artifact Storage

```
/artifacts/pca/{model_run_id}/model.json
```

---

## Phase 1: Dataset Sample Fetch Failure

### Symptom
```
Exception: failed to fetch samples
```

### Investigation

**Traced the code path:**

1. **UI Call:** `control_panel.dart:148`
   ```dart
   final samples = await service.getDatasetSamples(appState.datasetId!);
   ```

2. **Service Call:** `telemetry_service.dart:614-622`
   ```dart
   final response = await _client.get(Uri.parse('$baseUrl/datasets/$runId/samples?limit=$limit'));
   if (response.statusCode == 200) {
     final List items = jsonDecode(response.body)['items'];
     return items.map<Map<String, dynamic>>((m) => Map<String, dynamic>.from(m)).toList();
   }
   _handleError(response, 'Failed to get dataset samples');
   ```

3. **API Handler:** `api_server.cpp:477-489`
   ```cpp
   void ApiServer::HandleGetDatasetSamples(...) {
       auto data = db_client_->GetDatasetSamples(run_id, limit);
       nlohmann::json resp;
       resp["items"] = data;
       SendJson(res, resp, 200, rid);
   }
   ```

4. **DB Query:** `db_client.cpp:437-461`
   ```cpp
   auto res = N.exec_params(
       "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, "
       "network_tx_rate, metric_timestamp, host_id "
       "FROM host_telemetry_archival WHERE run_id = $1 ORDER BY metric_timestamp DESC LIMIT $2",
       run_id, limit);
   ```

### Root Cause

The sample fetch itself is correctly implemented. The "failed to fetch samples" error occurs as a **secondary failure** when:

1. The UI attempts to sync model state via `_syncWithAppState()` at line 172-179
2. This calls `service.getModelStatus(appState.modelRunId!)`
3. Which hits `GET /train/{id}` - **a route that is NOT registered**
4. The 404 error propagates and corrupts the UI state

**Evidence:** Looking at `api_server.cpp` lines 58-210, there is no registration for:
- `POST /train`
- `GET /train/{model_run_id}`
- `GET /models`

The handlers exist (`HandleTrainModel`, `HandleGetTrainStatus`, `HandleListModels`) at lines 543-622 but are never wired to routes.

### Conclusion

**This is NOT a sample fetch bug.** The sample fetch logic is correct. The failure is caused by missing route registrations that corrupt UI state during sync operations.

---

## Phase 2: Metrics Schema Load & Fallback Path

### Symptom
```
Failed to load metrics schema. Using fallback metrics.
```
(After fallback, nothing loads)

### Investigation

**Schema Endpoint:** `api_server.cpp:183-194`
```cpp
svr_.Get("/schema/metrics", [this](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json resp;
    resp["metrics"] = {
        {{"key", "cpu_usage"}, {"label", "CPU Usage"}, {"type", "numeric"}, ...},
        // ... 5 metrics total
    };
    SendJson(res, resp, 200, rid);
});
```

This endpoint is correctly registered and returns hardcoded schema.

**UI Schema Loading:** `dataset_analytics_screen.dart:41-71`
```dart
Future<void> _fetchSchema() async {
  try {
    final schema = await context.read<TelemetryService>().getMetricsSchema();
    // ...
  } catch (e) {
    debugPrint('Failed to load metrics schema: $e');
    setState(() {
      _availableMetrics = [/* fallback metrics */];
      _schemaError = 'Failed to load metrics schema. Using fallback metrics.';
    });
  }
}
```

**Service Implementation:** `telemetry_service.dart:463-471`
```dart
Future<List<Map<String, String>>> getMetricsSchema() async {
  final response = await _client.get(Uri.parse('$baseUrl/schema/metrics'));
  if (response.statusCode == 200) {
    final List metrics = jsonDecode(response.body)['metrics'];
    return metrics.map<Map<String, String>>((m) => Map<String, String>.from(m)).toList();
  }
  _handleError(response, 'Failed to get metrics schema');
}
```

### Root Cause

The schema endpoint IS working. The error message is misleading. The actual failure cascade is:

1. `_fetchSchema()` also calls `getDatasetMetricsSummary()` (line 53)
2. This triggers additional API calls
3. If the API server is unreachable OR if the `GET /models` call (made elsewhere in the app) fails with 404, the entire catch block is triggered
4. The fallback metrics ARE set correctly
5. The "nothing loads" issue is caused by **subsequent calls failing** due to:
   - Missing `GET /models` route (returns 404)
   - UI state corruption from failed model syncs

**Evidence:** The metrics schema route IS registered at line 183. The fallback metrics match the hardcoded schema exactly.

### Conclusion

**The schema endpoint works.** The "Failed to load metrics schema" error is a red herring caused by other network failures (missing routes) corrupting the error handling chain.

---

## Phase 3: Training Failure (COPY SELECT Syntax Error)

### Symptom
```
syntax error at or near "SELECT"
LINE 1: COPY SELECT cpu_usage, memory_usage, ...
```

### Investigation

**Training Entry Point:** `api_server.cpp:543-591`
```cpp
void ApiServer::HandleTrainModel(...) {
    // ... create DB entry ...
    job_manager_->StartJob("train-" + model_run_id, rid, [...] {
        auto artifact = telemetry::training::TrainPcaFromDb(db_conn_str_, dataset_id, 3, 99.5);
        // ...
    });
}
```

**CRITICAL BUG FOUND:** `pca_trainer.cpp:50-75`
```cpp
static void stream_samples(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           const std::function<void(const linalg::Vector&)>& on_sample) {
    pqxx::connection conn(db_conn_str);
    pqxx::work txn(conn);

    std::string query =
        "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
        "FROM host_telemetry_archival WHERE run_id = " + txn.quote(dataset_id) + " AND is_anomaly = false";

    pqxx::stream_from stream(txn, query);  // <-- BUG HERE
    // ...
}
```

### Root Cause

**The `pqxx::stream_from` constructor is being misused.**

Looking at the Dockerfile:
```dockerfile
libpqxx-6.4
```

In pqxx 6.4, the constructor `stream_from(transaction_base&, std::string_view)` expects a **table name**, not a query string.

When you pass a full SELECT statement as the second argument, pqxx interprets it as a table name and generates:
```sql
COPY SELECT cpu_usage, memory_usage, ... TO STDOUT
```

This is **invalid PostgreSQL syntax**. The correct syntax would be:
```sql
COPY (SELECT cpu_usage, memory_usage, ...) TO STDOUT
```

**Correct pqxx 6.4 usage for streaming query results:**
```cpp
// Option 1: Use stream_from::query() factory method (pqxx 7.x+)
// Not available in pqxx 6.4

// Option 2: Use raw COPY command
pqxx::stream_from stream = pqxx::stream_from::raw_table(txn,
    "host_telemetry_archival",
    {"cpu_usage", "memory_usage", "disk_utilization", "network_rx_rate", "network_tx_rate"});
// But this doesn't support WHERE clause!

// Option 3: Use a cursor-based approach
auto res = txn.exec(query);
for (const auto& row : res) {
    // process row
}
```

### Evidence

1. The error message `COPY SELECT cpu_usage...` shows no parentheses around the SELECT
2. pqxx 6.4 documentation confirms `stream_from(txn, string)` expects table name
3. The query string contains a complete SELECT statement

### Proposed Fix

Replace `pqxx::stream_from` with a standard query execution:

```cpp
static void stream_samples(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           const std::function<void(const linalg::Vector&)>& on_sample) {
    pqxx::connection conn(db_conn_str);
    pqxx::nontransaction txn(conn);

    std::string query =
        "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
        "FROM host_telemetry_archival WHERE run_id = " + txn.quote(dataset_id) + " AND is_anomaly = false";

    auto result = txn.exec(query);
    for (const auto& row : result) {
        linalg::Vector x(telemetry::anomaly::FeatureVector::kSize, 0.0);
        x[0] = row[0].as<double>();
        x[1] = row[1].as<double>();
        x[2] = row[2].as<double>();
        x[3] = row[3].as<double>();
        x[4] = row[4].as<double>();
        on_sample(x);
    }
}
```

**Note:** This loads all data into memory. For very large datasets, consider:
- Batch processing with `LIMIT/OFFSET`
- Using a server-side cursor
- Upgrading to pqxx 7.x which has better streaming support

---

## Phase 4: PCA Artifact Generation & Loading

### Symptom
```
Failed to load PCA Artifact
```

### Investigation

**Artifact Loading:** `pca_model.cpp:36-81`
```cpp
void PcaModel::Load(const std::string& artifact_path) {
    std::ifstream f(artifact_path);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open artifact: " + artifact_path);
    }
    // ... parse JSON ...
}
```

**Inference Handler:** `api_server.cpp:688-754`
```cpp
void ApiServer::HandleInference(...) {
    auto model_info = db_client_->GetModelRun(model_run_id);
    std::string artifact_path = model_info.value("artifact_path", "");
    if (artifact_path.empty()) {
        SendError(res, "Model is not yet complete or has no artifact", 400, ...);
        return;
    }
    try {
        pca.Load(artifact_path);
    } catch (const std::exception& e) {
        SendError(res, "Failed to load PCA model artifact: " + std::string(e.what()), 500, ...);
        return;
    }
}
```

### Root Cause

This is a **downstream failure** caused by the training bug (Phase 3):

1. Training fails due to `COPY SELECT` syntax error
2. `model_runs` table entry is created with `status='RUNNING'`
3. Training job throws exception
4. `UpdateModelRunStatus()` is called with `status='FAILED'` but `artifact_path` remains empty
5. Inference requests for this model fail because:
   - Either `artifact_path` is empty (400 error)
   - Or the artifact file doesn't exist (500 error with "Failed to open artifact")

**Evidence from `api_server.cpp:571-574`:**
```cpp
} catch (const std::exception& e) {
    spdlog::error("Training failed for model {}: {}", model_run_id, e.what());
    db_client_->UpdateModelRunStatus(model_run_id, "FAILED", "", e.what());
    throw;
}
```

The empty second argument to `UpdateModelRunStatus` means `artifact_path` is never set.

### Conclusion

**No artifact is ever created** because training always fails. Fix the training bug (Phase 3) and this issue resolves automatically.

---

## Cross-Cutting Concerns

### 1. Missing Route Registration (CRITICAL)

**File:** `api_server.cpp` constructor (lines 58-210)

**Missing Routes:**
| Route | Handler | Line Defined |
|-------|---------|--------------|
| `POST /train` | `HandleTrainModel` | 543 |
| `GET /train/{id}` | `HandleGetTrainStatus` | 593 |
| `GET /models` | `HandleListModels` | 604 |

**Fix Required:**
```cpp
// Add after line 161 (before static file mount)
svr_.Post("/train", [this](const httplib::Request& req, httplib::Response& res) {
    HandleTrainModel(req, res);
});

svr_.Get("/train/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
    HandleGetTrainStatus(req, res);
});

svr_.Get("/models", [this](const httplib::Request& req, httplib::Response& res) {
    HandleListModels(req, res);
});
```

### 2. Broken Foreign Key Constraint

**File:** `db/init.sql:138`

```sql
record_id BIGINT NOT NULL REFERENCES host_telemetry_archival(record_id),
```

**Problem:** `host_telemetry_archival` is a **partitioned table** with composite primary key `(ingestion_time, record_id)`. PostgreSQL does not support foreign keys referencing a single column of a composite primary key in partitioned tables.

**This will cause:**
- FK constraint creation failure (silently ignored with `IF NOT EXISTS`)
- Potential data integrity issues

**Fix Options:**
1. Remove the FK constraint (acceptable for this use case)
2. Add `ingestion_time` to `dataset_scores` and reference the full composite key
3. Create a unique constraint on `record_id` alone (if globally unique)

### 3. Error Masking in UI

The Flutter UI's error handling (e.g., `_handleError`) converts all HTTP errors into generic messages. Consider:
- Preserving error codes
- Distinguishing between network errors and application errors
- Not propagating unrelated failures to the user

---

## Root Cause Summary

| Symptom | Root Cause | File:Line |
|---------|------------|-----------|
| Dataset Selection Failure | Missing route registrations cause 404, corrupting UI state | `api_server.cpp:58-210` |
| COPY SELECT Error | Wrong `stream_from` constructor usage | `pca_trainer.cpp:59` |
| Failed to load PCA Artifact | Training never succeeds, no artifact created | Downstream of training bug |
| Metrics Schema Fallback | Not a real failure; other 404s corrupt error chain | Cascading from missing routes |

**Causal Chain:**
```
Missing Routes → 404 on GET /models, GET /train/{id}, POST /train
                          ↓
                  UI state corruption
                          ↓
                  "Failed to fetch samples" (misleading)

Wrong stream_from usage → COPY SELECT syntax error
                          ↓
                  Training always fails
                          ↓
                  No artifact created
                          ↓
                  "Failed to load PCA Artifact"
```

---

## Proposed Fix Plan

### Priority 1: Critical (Blocks All Functionality)

#### Fix 1.1: Register Missing Routes
**File:** `src/api_server.cpp`
**Location:** After line 161, before static file mount

```cpp
svr_.Post("/train", [this](const httplib::Request& req, httplib::Response& res) {
    HandleTrainModel(req, res);
});

svr_.Get("/train/([a-zA-Z0-9-]+)", [this](const httplib::Request& req, httplib::Response& res) {
    HandleGetTrainStatus(req, res);
});

svr_.Get("/models", [this](const httplib::Request& req, httplib::Response& res) {
    HandleListModels(req, res);
});
```

#### Fix 1.2: Fix Training Data Streaming
**File:** `src/training/pca_trainer.cpp`
**Location:** Lines 50-75

Replace `pqxx::stream_from` with standard query execution:

```cpp
static void stream_samples(const std::string& db_conn_str,
                           const std::string& dataset_id,
                           const std::function<void(const linalg::Vector&)>& on_sample) {
    pqxx::connection conn(db_conn_str);
    pqxx::nontransaction N(conn);

    auto result = N.exec_params(
        "SELECT cpu_usage, memory_usage, disk_utilization, network_rx_rate, network_tx_rate "
        "FROM host_telemetry_archival WHERE run_id = $1 AND is_anomaly = false",
        dataset_id);

    for (const auto& row : result) {
        linalg::Vector x(telemetry::anomaly::FeatureVector::kSize, 0.0);
        x[0] = row[0].as<double>();
        x[1] = row[1].as<double>();
        x[2] = row[2].as<double>();
        x[3] = row[3].as<double>();
        x[4] = row[4].as<double>();
        on_sample(x);
    }
}
```

### Priority 2: Database Schema Fix

#### Fix 2.1: Remove Broken FK Constraint
**File:** `db/init.sql` (and migration)

Option A - Remove FK:
```sql
CREATE TABLE IF NOT EXISTS dataset_scores (
    score_id BIGSERIAL PRIMARY KEY,
    dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
    model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
    record_id BIGINT NOT NULL,  -- Remove REFERENCES
    reconstruction_error DOUBLE PRECISION NOT NULL,
    predicted_is_anomaly BOOLEAN NOT NULL,
    scored_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

### Priority 3: Structural Improvements

#### Fix 3.1: Add CORS for DELETE method
Currently CORS only allows `GET, POST, OPTIONS`. Add `DELETE` for job cancellation.

#### Fix 3.2: Improve Error Messages
Add request IDs to all error responses for debugging.

---

## Test Plan

### Test 1: Route Registration
```bash
# Should return 200 with models list
curl -X GET http://localhost:8280/models

# Should return 202 with model_run_id
curl -X POST http://localhost:8280/train \
  -H "Content-Type: application/json" \
  -d '{"dataset_id": "<uuid>", "name": "test_model"}'

# Should return 200 with model status
curl -X GET http://localhost:8280/train/<model_run_id>
```

### Test 2: Training Pipeline
1. Generate a dataset with 5 hosts
2. Start training via `POST /train`
3. Poll `GET /train/{id}` until status is `COMPLETED`
4. Verify artifact exists at `artifacts/pca/{model_run_id}/model.json`
5. Run inference via `POST /inference`

### Test 3: End-to-End UI
1. Load Control Panel
2. Generate dataset - verify polling works
3. Train model - verify status updates
4. Run inference - verify results display

### Test 4: Database Integrity
```sql
-- Verify scores can be inserted
INSERT INTO dataset_scores (dataset_id, model_run_id, record_id, reconstruction_error, predicted_is_anomaly)
VALUES ('<dataset_uuid>', '<model_uuid>', 1, 0.5, false);
```

---

## Recommendations

1. **Immediate:** Apply Fix 1.1 and 1.2 - these unblock all functionality
2. **Short-term:** Apply Fix 2.1 to prevent FK errors during scoring
3. **Medium-term:** Consider upgrading to pqxx 7.x for better streaming support
4. **Long-term:** Add integration tests that verify all routes are registered

---

## Appendix: Key File References

| File | Purpose |
|------|---------|
| `src/api_server.cpp` | HTTP API route registration and handlers |
| `src/db_client.cpp` | Database access layer |
| `src/training/pca_trainer.cpp` | PCA model training logic |
| `src/detectors/pca_model.cpp` | PCA inference/scoring |
| `web_ui/lib/services/telemetry_service.dart` | Flutter HTTP client |
| `web_ui/lib/screens/control_panel.dart` | Main UI workflow |
| `db/init.sql` | Database schema |
