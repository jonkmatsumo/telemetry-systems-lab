-- Migration: Add updated_at to generation_runs and model_runs for heartbeat support

ALTER TABLE generation_runs ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();

CREATE INDEX IF NOT EXISTS idx_generation_runs_updated_at ON generation_runs(updated_at);
CREATE INDEX IF NOT EXISTS idx_model_runs_updated_at ON model_runs(updated_at);
CREATE INDEX IF NOT EXISTS idx_score_jobs_updated_at ON dataset_score_jobs(updated_at);
