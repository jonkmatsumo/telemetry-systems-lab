import 'dart:convert';
import 'dart:async';
import 'package:http/http.dart' as http;

class DatasetStatus {
  final String runId;
  final String status;
  final int rowsInserted;

  DatasetStatus({required this.runId, required this.status, required this.rowsInserted});

  factory DatasetStatus.fromJson(Map<String, dynamic> json) {
    return DatasetStatus(
      runId: json['run_id'] ?? '',
      status: json['status'] ?? '',
      rowsInserted: json['rows_inserted'] ?? 0,
    );
  }
}

class ModelStatus {
  final String modelRunId;
  final String datasetId;
  final String name;
  final String status;
  final String? artifactPath;
  final String? error;

  ModelStatus({
    required this.modelRunId,
    required this.datasetId,
    required this.name,
    required this.status,
    this.artifactPath,
    this.error,
  });

  factory ModelStatus.fromJson(Map<String, dynamic> json) {
    return ModelStatus(
      modelRunId: json['model_run_id'] ?? '',
      datasetId: json['dataset_id'] ?? '',
      name: json['name'] ?? '',
      status: json['status'] ?? '',
      artifactPath: json['artifact_path'],
      error: json['error'],
    );
  }
}

class InferenceResult {
  final bool isAnomaly;
  final double score;

  InferenceResult({required this.isAnomaly, required this.score});

  factory InferenceResult.fromJson(Map<String, dynamic> json) {
    return InferenceResult(
      isAnomaly: json['is_anomaly'] ?? false,
      score: (json['score'] ?? 0.0).toDouble(),
    );
  }
}

class InferenceResponse {
  final List<InferenceResult> results;
  final String modelRunId;
  final String inferenceId;
  final int anomalyCount;

  InferenceResponse({
    required this.results,
    required this.modelRunId,
    required this.inferenceId,
    required this.anomalyCount,
  });

  factory InferenceResponse.fromJson(Map<String, dynamic> json) {
    return InferenceResponse(
      results: (json['results'] as List? ?? [])
          .map((v) => InferenceResult.fromJson(v as Map<String, dynamic>))
          .toList(),
      modelRunId: json['model_run_id'] ?? '',
      inferenceId: json['inference_id'] ?? '',
      anomalyCount: json['anomaly_count'] ?? 0,
    );
  }
}

class TelemetryService {
  final String baseUrl = 'http://localhost:8080';

  Future<String> generateDataset(int hostCount) async {
    final response = await http.post(
      Uri.parse('$baseUrl/datasets'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'host_count': hostCount}),
    );
    if (response.statusCode == 200 || response.statusCode == 201 || response.statusCode == 202) {
      return jsonDecode(response.body)['run_id'];
    }
    throw Exception('Failed to generate dataset: ${response.body}');
  }

  Future<DatasetStatus> getDatasetStatus(String id) async {
    final response = await http.get(Uri.parse('$baseUrl/datasets/$id'));
    if (response.statusCode == 200) {
      return DatasetStatus.fromJson(jsonDecode(response.body));
    }
    throw Exception('Failed to get dataset status: ${response.body}');
  }

  Future<String> trainModel(String datasetId, {String name = 'pca_default'}) async {
    final response = await http.post(
      Uri.parse('$baseUrl/train'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'dataset_id': datasetId, 'name': name}),
    );
    if (response.statusCode == 202 || response.statusCode == 200) {
      return jsonDecode(response.body)['model_run_id'];
    }
    throw Exception('Failed to train model: ${response.body}');
  }

  Future<ModelStatus> getModelStatus(String id) async {
    final response = await http.get(Uri.parse('$baseUrl/train/$id'));
    if (response.statusCode == 200) {
      return ModelStatus.fromJson(jsonDecode(response.body));
    }
    throw Exception('Failed to get model status: ${response.body}');
  }

  Future<InferenceResponse> runInference(String modelId, List<Map<String, dynamic>> samples) async {
    final response = await http.post(
      Uri.parse('$baseUrl/inference'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'model_run_id': modelId, 'samples': samples}),
    );
    if (response.statusCode == 200) {
      return InferenceResponse.fromJson(jsonDecode(response.body));
    }
    throw Exception('Failed to run inference: ${response.body}');
  }
}
