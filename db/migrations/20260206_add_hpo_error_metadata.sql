-- Phase 2.1: Better Failure Modes and Recovery
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS error_summary JSONB NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS error_aggregates JSONB NULL;
