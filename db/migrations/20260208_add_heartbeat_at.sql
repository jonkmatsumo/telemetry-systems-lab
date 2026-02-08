-- Migration: Add heartbeat_at to jobs tables
ALTER TABLE generation_runs ADD COLUMN IF NOT EXISTS heartbeat_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS heartbeat_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE dataset_score_jobs ADD COLUMN IF NOT EXISTS heartbeat_at TIMESTAMPTZ NOT NULL DEFAULT NOW();

CREATE INDEX IF NOT EXISTS idx_generation_runs_heartbeat ON generation_runs(heartbeat_at) WHERE status IN ('RUNNING', 'PENDING');
CREATE INDEX IF NOT EXISTS idx_model_runs_heartbeat ON model_runs(heartbeat_at) WHERE status IN ('RUNNING', 'PENDING');
CREATE INDEX IF NOT EXISTS idx_score_jobs_heartbeat ON dataset_score_jobs(heartbeat_at) WHERE status IN ('RUNNING', 'PENDING');
