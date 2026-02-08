#include "route_registry.h"

namespace telemetry::api {

const std::vector<RouteSpec> kRequiredRoutes = {
    {"POST", "/datasets", "CreateDataset"},
    {"GET", "/datasets", "ListDatasets"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)", "GetDataset"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/summary", "GetDatasetSummary"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/topk", "GetDatasetTopK"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/timeseries", "GetDatasetTimeSeries"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/histogram", "GetDatasetHistogram"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/samples", "GetDatasetSamples"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/records/([0-9]+)", "GetDatasetRecord"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/metrics/([a-zA-Z0-9_]+)/stats", "GetMetricStats"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/metrics/summary", "GetMetricsSummary"},
    {"GET", "/datasets/([a-zA-Z0-9-]+)/models", "GetDatasetModels"},
    {"GET", "/models/([a-zA-Z0-9-]+)", "GetModel"},
    {"GET", "/models/([a-zA-Z0-9-]+)/datasets/scored", "GetModelScoredDatasets"},
    {"GET", "/scores", "GetScores"},
    {"POST", "/inference", "RunInference"},
    {"GET", "/inference_runs", "ListInferenceRuns"},
    {"GET", "/inference_runs/([a-zA-Z0-9-]+)", "GetInferenceRun"},
    {"POST", "/jobs/score_dataset", "CreateScoreJob"},
    {"GET", "/jobs", "ListJobs"},
    {"GET", "/jobs/([a-zA-Z0-9-]+)/progress", "GetJobProgress"},
    {"GET", "/jobs/([a-zA-Z0-9-]+)", "GetJob"},
    {"GET", "/models/([a-zA-Z0-9-]+)/eval", "GetModelEval"},
    {"GET", "/models/([a-zA-Z0-9-]+)/error_distribution", "GetModelErrorDist"},
    {"GET", "/healthz", "HealthCheck"},
    {"GET", "/readyz", "ReadyCheck"},
    {"GET", "/metrics", "Metrics"},
    {"GET", "/schema/metrics", "GetMetricsSchema"},
    {"POST", "/train", "StartTrain"},
    {"GET", "/train/([a-zA-Z0-9-]+)", "GetTrainStatus"},
    {"DELETE", "/train/([a-zA-Z0-9-]+)", "CancelTrain"},
    {"GET", "/models", "ListModels"},
    {"GET", "/models/([a-zA-Z0-9-]+)/trials", "GetHpoTrials"},
    {"POST", "/models/([a-zA-Z0-9-]+)/rerun_failed", "RerunFailedTrials"},
    {"DELETE", "/jobs/([a-zA-Z0-9-]+)", "CancelJob"}
};

} // namespace telemetry::api