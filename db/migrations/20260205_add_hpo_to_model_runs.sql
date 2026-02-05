-- Phase 1.2: HPO Persistence
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS hpo_config JSONB NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS parent_run_id UUID NULL REFERENCES model_runs(model_run_id);
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS trial_index INT NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS trial_params JSONB NULL;

CREATE INDEX IF NOT EXISTS idx_model_runs_parent_id ON model_runs(parent_run_id);
