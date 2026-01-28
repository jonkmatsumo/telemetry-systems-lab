import 'package:flutter/material.dart';

class AppState extends ChangeNotifier {
  String? datasetId;
  String? modelRunId;
  String? startTime;
  String? endTime;
  bool? isAnomaly;
  String? anomalyType;

  void setDataset(String? id) {
    datasetId = id;
    notifyListeners();
  }

  void setModel(String? id) {
    modelRunId = id;
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
