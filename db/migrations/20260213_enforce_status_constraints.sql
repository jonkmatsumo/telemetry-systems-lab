BEGIN;

UPDATE generation_runs SET status = 'CANCELLED' WHERE status = 'CANCELED';
UPDATE model_runs SET status = 'CANCELLED' WHERE status = 'CANCELED';
UPDATE inference_runs SET status = 'CANCELLED' WHERE status = 'CANCELED';
UPDATE dataset_score_jobs SET status = 'CANCELLED' WHERE status = 'CANCELED';

ALTER TABLE generation_runs DROP CONSTRAINT IF EXISTS chk_generation_runs_status;
ALTER TABLE generation_runs
    ADD CONSTRAINT chk_generation_runs_status
    CHECK (status IN ('PENDING', 'RUNNING', 'SUCCEEDED', 'FAILED', 'CANCELLED'));

ALTER TABLE model_runs DROP CONSTRAINT IF EXISTS chk_model_runs_status;
ALTER TABLE model_runs
    ADD CONSTRAINT chk_model_runs_status
    CHECK (status IN ('PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'));

ALTER TABLE inference_runs DROP CONSTRAINT IF EXISTS chk_inference_runs_status;
ALTER TABLE inference_runs
    ADD CONSTRAINT chk_inference_runs_status
    CHECK (status IN ('PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'));

ALTER TABLE dataset_score_jobs DROP CONSTRAINT IF EXISTS chk_dataset_score_jobs_status;
ALTER TABLE dataset_score_jobs
    ADD CONSTRAINT chk_dataset_score_jobs_status
    CHECK (status IN ('PENDING', 'RUNNING', 'COMPLETED', 'FAILED', 'CANCELLED'));

COMMIT;
