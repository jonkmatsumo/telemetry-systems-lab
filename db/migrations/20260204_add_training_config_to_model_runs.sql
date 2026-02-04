-- Phase 1: Training Configuration Capture
ALTER TABLE model_runs ADD COLUMN IF NOT EXISTS training_config JSONB NOT NULL DEFAULT '{}';
