import 'package:flutter/material.dart';
import '../services/telemetry_service.dart';

class AppState extends ChangeNotifier {
  String? datasetId;
  String? modelRunId;
  String? startTime;
  String? endTime;
  bool? isAnomaly;
  String? anomalyType;

  DatasetStatus? currentDataset;
  ModelStatus? currentModel;

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

  void setDatasetStatus(DatasetStatus? status) {
    currentDataset = status;
    if (status != null) datasetId = status.runId;
    notifyListeners();
  }

  void setModelStatus(ModelStatus? status) {
    currentModel = status;
    if (status != null) modelRunId = status.modelRunId;
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
