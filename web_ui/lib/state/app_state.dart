import 'package:flutter/material.dart';
import '../services/telemetry_service.dart';

class AppState extends ChangeNotifier {
  String? datasetId;
  String? modelRunId;
  String? startTime;
  String? endTime;
  bool? isAnomaly;
  String? anomalyType;

  int currentTabIndex = 0;

  void setTabIndex(int index) {
    currentTabIndex = index;
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
}
