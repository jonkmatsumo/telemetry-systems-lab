-- Phase 2.3: Best Trial Tracking
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS best_trial_run_id UUID NULL REFERENCES model_runs(model_run_id);
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS best_metric_value DOUBLE PRECISION NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS best_metric_name TEXT NULL;
