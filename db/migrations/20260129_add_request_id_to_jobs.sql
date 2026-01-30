-- Migration: Add request_id to job tracking tables for traceability across restarts
ALTER TABLE dataset_score_jobs ADD COLUMN IF NOT EXISTS request_id TEXT;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS request_id TEXT;
ALTER TABLE generation_runs ADD COLUMN IF NOT EXISTS request_id TEXT;

-- Index for correlation lookups
CREATE INDEX IF NOT EXISTS idx_score_jobs_request_id ON dataset_score_jobs(request_id);
CREATE INDEX IF NOT EXISTS idx_model_runs_request_id ON model_runs(request_id);
CREATE INDEX IF NOT EXISTS idx_generation_runs_request_id ON generation_runs(request_id);
