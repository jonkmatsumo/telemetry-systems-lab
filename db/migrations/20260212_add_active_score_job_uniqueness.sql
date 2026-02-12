BEGIN;

WITH ranked_active AS (
    SELECT
        job_id,
        ROW_NUMBER() OVER (
            PARTITION BY dataset_id, model_run_id
            ORDER BY created_at DESC, job_id DESC
        ) AS rn
    FROM dataset_score_jobs
    WHERE status IN ('PENDING', 'RUNNING')
)
UPDATE dataset_score_jobs j
SET status = 'FAILED',
    error = COALESCE(j.error, 'Superseded by active score job uniqueness constraint'),
    updated_at = NOW(),
    heartbeat_at = NOW(),
    completed_at = COALESCE(j.completed_at, NOW())
FROM ranked_active r
WHERE j.job_id = r.job_id
  AND r.rn > 1;

CREATE UNIQUE INDEX IF NOT EXISTS idx_score_jobs_active_unique
    ON dataset_score_jobs(dataset_id, model_run_id)
    WHERE status IN ('PENDING', 'RUNNING');

COMMIT;
