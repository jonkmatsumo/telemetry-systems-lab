-- Phase 1.1: HPO Safety, Reproducibility, and Limits
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS candidate_fingerprint TEXT NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS generator_version TEXT NULL;
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS seed_used BIGINT NULL;
