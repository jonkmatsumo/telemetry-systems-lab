-- Phase 2.1: Best-Trial Determinism and Transparency
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS selection_metric_direction TEXT NULL; -- LOWER_IS_BETTER, HIGHER_IS_BETTER
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS tie_break_basis TEXT NULL; -- completion_time, trial_index
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS is_eligible BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS eligibility_reason TEXT NULL; -- FAILED, MISSING_ARTIFACT, CANCELED
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS selection_metric_value DOUBLE PRECISION NULL;
