import 'package:flutter/material.dart';
import '../services/telemetry_service.dart';
import 'investigation_context.dart';

class PendingInferenceRequest {
  final String datasetId;
  final String modelId;
  final String recordId;
  final Map<String, dynamic>? recordPayload;

  PendingInferenceRequest({
    required this.datasetId,
    required this.modelId,
    required this.recordId,
    this.recordPayload,
  });
}

class AppState extends ChangeNotifier {
  String? datasetId;
  String? modelRunId;
  String? startTime;
  String? endTime;
  bool? isAnomaly;
  String? anomalyType;
  String? filterRegion;
  String? filterAnomalyType;
  String? filterBucketStart;
  String? filterBucketEnd;

  int currentTabIndex = 0;
  bool useUtc = false;

  PendingInferenceRequest? pendingInference;
  InvestigationContext? investigationContext;

  void setTabIndex(int index) {
    currentTabIndex = index;
    notifyListeners();
  }

  void setUseUtc(bool value) {
    useUtc = value;
    notifyListeners();
  }

  void setPendingInference(PendingInferenceRequest? req) {
    pendingInference = req;
    notifyListeners();
  }

  void clearPendingInference() {
    pendingInference = null;
    notifyListeners();
  }

  void setInvestigationContext(InvestigationContext? ctx) {
    investigationContext = ctx;
    notifyListeners();
  }

  DatasetStatus? currentDataset;
  ModelStatus? currentModel;

  List<ScoreJobStatus> activeJobs = [];
  final Set<String> _clearedJobIds = {};

  void updateJobs(List<ScoreJobStatus> jobs) {
    activeJobs = jobs.where((j) => !_clearedJobIds.contains(j.jobId)).toList();
    notifyListeners();
  }

  void clearJob(String jobId) {
    _clearedJobIds.add(jobId);
    activeJobs.removeWhere((j) => j.jobId == jobId);
    notifyListeners();
  }

  void clearCompletedJobs() {
    final completed = activeJobs.where((j) => j.status == 'COMPLETED' || j.status == 'FAILED' || j.status == 'CANCELLED');
    for (final j in completed) {
      _clearedJobIds.add(j.jobId);
    }
    activeJobs.removeWhere((j) => j.status == 'COMPLETED' || j.status == 'FAILED' || j.status == 'CANCELLED');
    notifyListeners();
  }

  final Map<String, String> _datasetMetrics = {};

  String getSelectedMetric(String datasetId) => _datasetMetrics[datasetId] ?? 'cpu_usage';

  void setSelectedMetric(String datasetId, String metric) {
    _datasetMetrics[datasetId] = metric;
    notifyListeners();
  }

  void setDataset(String? id, {DatasetStatus? status}) {
    datasetId = id;
    currentDataset = status;
    notifyListeners();
  }

  void setModel(String? id, {ModelStatus? status}) {
    modelRunId = id;
    currentModel = status;
    notifyListeners();
  }

  void setTimeRange({String? start, String? end}) {
    startTime = start;
    endTime = end;
    notifyListeners();
  }

  void setIsAnomaly(bool? value) {
    isAnomaly = value;
    notifyListeners();
  }

  void setAnomalyType(String? value) {
    anomalyType = value;
    notifyListeners();
  }

  void setFilterRegion(String? value) {
    filterRegion = value;
    notifyListeners();
  }

  void setFilterAnomalyType(String? value) {
    filterAnomalyType = value;
    notifyListeners();
  }

  void setFilterBucket(String? start, String? end) {
    filterBucketStart = start;
    filterBucketEnd = end;
    notifyListeners();
  }

  void clearFilters() {
    filterRegion = null;
    filterAnomalyType = null;
    filterBucketStart = null;
    filterBucketEnd = null;
    notifyListeners();
  }
}
