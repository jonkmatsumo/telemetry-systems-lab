-- Create tables for Telemetry Generator MVP

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Table: generation_runs
-- Tracks metadata for each generation job
CREATE TABLE IF NOT EXISTS generation_runs (
    run_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    tier TEXT NOT NULL,
    host_count INT NOT NULL,
    start_time TIMESTAMPTZ NOT NULL,
    end_time TIMESTAMPTZ NOT NULL,
    interval_seconds INT NOT NULL,
    seed BIGINT NOT NULL,
    status TEXT NOT NULL, -- PENDING, RUNNING, SUCCEEDED, FAILED, CANCELLED
    inserted_rows BIGINT NOT NULL DEFAULT 0,
    config JSONB NOT NULL,
    error TEXT NULL,
    request_id TEXT NULL
);

-- Table: host_telemetry_archival
-- Main time-series storage, partitioned by ingestion_time for lifecycle management
CREATE TABLE IF NOT EXISTS host_telemetry_archival (
    record_id BIGSERIAL,
    ingestion_time TIMESTAMPTZ NOT NULL,
    metric_timestamp TIMESTAMPTZ NOT NULL,
    host_id TEXT NOT NULL,
    project_id TEXT NOT NULL,
    region TEXT NOT NULL,
    cpu_usage DOUBLE PRECISION NOT NULL,
    memory_usage DOUBLE PRECISION NOT NULL,
    disk_utilization DOUBLE PRECISION NOT NULL,
    network_rx_rate DOUBLE PRECISION NOT NULL,
    network_tx_rate DOUBLE PRECISION NOT NULL,
    labels JSONB NOT NULL,
    run_id UUID NOT NULL REFERENCES generation_runs(run_id),
    is_anomaly BOOLEAN NOT NULL DEFAULT FALSE,
    anomaly_type TEXT NULL,
    PRIMARY KEY (ingestion_time, record_id)
) PARTITION BY RANGE (ingestion_time);

-- Default partition for catching data outside specific ranges
CREATE TABLE IF NOT EXISTS host_telemetry_archival_default 
    PARTITION OF host_telemetry_archival DEFAULT;

-- Initial partitions (e.g. for Jan 2026)
CREATE TABLE IF NOT EXISTS host_telemetry_archival_2026_01 
    PARTITION OF host_telemetry_archival 
    FOR VALUES FROM ('2026-01-01') TO ('2026-02-01');

CREATE TABLE IF NOT EXISTS host_telemetry_archival_2026_02 
    PARTITION OF host_telemetry_archival 
    FOR VALUES FROM ('2026-02-01') TO ('2026-03-01');

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_telemetry_run_id ON host_telemetry_archival(run_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_run_anomaly ON host_telemetry_archival(run_id, is_anomaly);
CREATE INDEX IF NOT EXISTS idx_telemetry_host_ts ON host_telemetry_archival(host_id, metric_timestamp);
CREATE INDEX IF NOT EXISTS idx_telemetry_region_ts ON host_telemetry_archival(region, metric_timestamp);
CREATE INDEX IF NOT EXISTS idx_telemetry_run_ts ON host_telemetry_archival(run_id, metric_timestamp);
CREATE INDEX IF NOT EXISTS idx_telemetry_run_type ON host_telemetry_archival(run_id, anomaly_type);
CREATE INDEX IF NOT EXISTS idx_generation_runs_request_id ON generation_runs(request_id);
-- BRIN index is good for naturally ordered time-series data
CREATE INDEX IF NOT EXISTS idx_telemetry_brin_ts ON host_telemetry_archival USING BRIN(metric_timestamp);
-- GIN index for JSONB labels querying
CREATE INDEX IF NOT EXISTS idx_telemetry_labels ON host_telemetry_archival USING GIN(labels);

-- Table: alerts
-- Stores anomalies detected by fusion engine
CREATE TABLE IF NOT EXISTS alerts (
    alert_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    host_id TEXT NOT NULL,
    run_id UUID NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL,
    severity TEXT NOT NULL, -- LOW, MEDIUM, HIGH, CRITICAL
    detector_source TEXT NOT NULL, -- "DETECTOR_A", "DETECTOR_B", "FUSION"
    score DOUBLE PRECISION NOT NULL,
    details JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_alerts_run_id ON alerts(run_id);
CREATE INDEX IF NOT EXISTS idx_alerts_host_id ON alerts(host_id);

-- Table: model_runs
-- Tracks training jobs and artifacts
CREATE TABLE IF NOT EXISTS model_runs (
    model_run_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
    name TEXT NOT NULL, -- e.g. "pca_default"
    status TEXT NOT NULL, -- PENDING, RUNNING, COMPLETED, FAILED
    artifact_path TEXT NULL,
    error TEXT NULL,
    request_id TEXT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    completed_at TIMESTAMPTZ NULL
);

-- Table: inference_runs
-- Tracks inference requests and outcomes
CREATE TABLE IF NOT EXISTS inference_runs (
    inference_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
    status TEXT NOT NULL,
    anomaly_count INT NOT NULL DEFAULT 0,
    details JSONB NULL,
    latency_ms DOUBLE PRECISION NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_model_runs_dataset_id ON model_runs(dataset_id);
CREATE INDEX IF NOT EXISTS idx_model_runs_request_id ON model_runs(request_id);
CREATE INDEX IF NOT EXISTS idx_inference_model_id ON inference_runs(model_run_id);

-- Table: dataset_score_jobs
-- Tracks background scoring jobs
CREATE TABLE IF NOT EXISTS dataset_score_jobs (
    job_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
    model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
    status TEXT NOT NULL, -- PENDING, RUNNING, COMPLETED, FAILED
    total_rows BIGINT NOT NULL DEFAULT 0,
    processed_rows BIGINT NOT NULL DEFAULT 0,
    last_record_id BIGINT NOT NULL DEFAULT 0,
    error TEXT NULL,
    request_id TEXT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    completed_at TIMESTAMPTZ NULL
);

CREATE INDEX IF NOT EXISTS idx_score_jobs_request_id ON dataset_score_jobs(request_id);
CREATE INDEX IF NOT EXISTS idx_scores_dataset_model ON dataset_scores(dataset_id, model_run_id);
