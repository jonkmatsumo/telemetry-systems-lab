-- Phase 3.1: Metric Integrity and Comparison Improvements
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS selection_metric_source TEXT NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS selection_metric_computed_at TIMESTAMPTZ NULL;
