BEGIN;

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

CREATE TABLE IF NOT EXISTS model_runs (
  model_run_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
  name TEXT NOT NULL,
  status TEXT NOT NULL,
  artifact_path TEXT NULL,
  error TEXT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  completed_at TIMESTAMPTZ NULL
);

CREATE TABLE IF NOT EXISTS inference_runs (
  inference_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
  status TEXT NOT NULL,
  anomaly_count INT NOT NULL DEFAULT 0,
  details JSONB NULL,
  latency_ms DOUBLE PRECISION NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE IF EXISTS host_telemetry_archival
  ADD COLUMN IF NOT EXISTS record_id BIGSERIAL PRIMARY KEY;

CREATE INDEX IF NOT EXISTS idx_telemetry_run_ts ON host_telemetry_archival(run_id, metric_timestamp);
CREATE INDEX IF NOT EXISTS idx_telemetry_run_type ON host_telemetry_archival(run_id, anomaly_type);

CREATE INDEX IF NOT EXISTS idx_model_runs_dataset_id ON model_runs(dataset_id);
CREATE INDEX IF NOT EXISTS idx_inference_model_id ON inference_runs(model_run_id);

CREATE TABLE IF NOT EXISTS dataset_score_jobs (
  job_id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
  model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
  status TEXT NOT NULL,
  total_rows BIGINT NOT NULL DEFAULT 0,
  processed_rows BIGINT NOT NULL DEFAULT 0,
  error TEXT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  completed_at TIMESTAMPTZ NULL
);

CREATE TABLE IF NOT EXISTS dataset_scores (
  score_id BIGSERIAL PRIMARY KEY,
  dataset_id UUID NOT NULL REFERENCES generation_runs(run_id),
  model_run_id UUID NOT NULL REFERENCES model_runs(model_run_id),
  record_id BIGINT NOT NULL REFERENCES host_telemetry_archival(record_id),
  reconstruction_error DOUBLE PRECISION NOT NULL,
  predicted_is_anomaly BOOLEAN NOT NULL,
  scored_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_scores_dataset_model ON dataset_scores(dataset_id, model_run_id);
CREATE INDEX IF NOT EXISTS idx_scores_record_id ON dataset_scores(record_id);

COMMIT;
