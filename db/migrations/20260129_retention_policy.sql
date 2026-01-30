-- Migration: Add retention policy management
-- For non-partitioned tables, we provide a manual cleanup procedure.
-- For partitioned tables (new installs), this could be extended to DROP TABLE.

CREATE OR REPLACE PROCEDURE cleanup_old_telemetry(retention_days INT)
LANGUAGE plpgsql
AS $$
BEGIN
    -- Cleanup telemetry data
    DELETE FROM host_telemetry_archival 
    WHERE ingestion_time < NOW() - (retention_days || ' days')::interval;
    
    -- Cleanup related scores if orphaned
    DELETE FROM dataset_scores
    WHERE record_id NOT IN (SELECT record_id FROM host_telemetry_archival);
    
    COMMIT;
END;
$$;
