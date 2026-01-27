import 'dart:convert';
import 'dart:async';
import 'package:http/http.dart' as http;

class DatasetRun {
  final String runId;
  final String status;
  final int insertedRows;
  final String createdAt;
  final String startTime;
  final String endTime;
  final int intervalSeconds;
  final int hostCount;
  final String tier;

  DatasetRun({
    required this.runId,
    required this.status,
    required this.insertedRows,
    required this.createdAt,
    required this.startTime,
    required this.endTime,
    required this.intervalSeconds,
    required this.hostCount,
    required this.tier,
  });

  factory DatasetRun.fromJson(Map<String, dynamic> json) {
    return DatasetRun(
      runId: json['run_id'] ?? '',
      status: json['status'] ?? '',
      insertedRows: json['inserted_rows'] ?? 0,
      createdAt: json['created_at'] ?? '',
      startTime: json['start_time'] ?? '',
      endTime: json['end_time'] ?? '',
      intervalSeconds: json['interval_seconds'] ?? 0,
      hostCount: json['host_count'] ?? 0,
      tier: json['tier'] ?? '',
    );
  }
}

class DatasetSummary {
  final int rowCount;
  final String minTs;
  final String maxTs;
  final double anomalyRate;
  final Map<String, int> distinctCounts;
  final List<Map<String, dynamic>> anomalyTypeCounts;

  DatasetSummary({
    required this.rowCount,
    required this.minTs,
    required this.maxTs,
    required this.anomalyRate,
    required this.distinctCounts,
    required this.anomalyTypeCounts,
  });

  factory DatasetSummary.fromJson(Map<String, dynamic> json) {
    final range = json['time_range'] ?? {};
    final distinct = json['distinct_counts'] ?? {};
    return DatasetSummary(
      rowCount: json['row_count'] ?? 0,
      minTs: range['min_ts'] ?? '',
      maxTs: range['max_ts'] ?? '',
      anomalyRate: (json['anomaly_rate'] ?? 0.0).toDouble(),
      distinctCounts: {
        'host_id': distinct['host_id'] ?? 0,
        'project_id': distinct['project_id'] ?? 0,
        'region': distinct['region'] ?? 0,
      },
      anomalyTypeCounts: (json['anomaly_type_counts'] as List? ?? [])
          .map((e) => Map<String, dynamic>.from(e as Map))
          .toList(),
    );
  }
}

class TopKEntry {
  final String label;
  final int count;

  TopKEntry({required this.label, required this.count});

  factory TopKEntry.fromJson(Map<String, dynamic> json) {
    return TopKEntry(
      label: json['label'] ?? '',
      count: json['count'] ?? 0,
    );
  }
}

class TimeSeriesPoint {
  final String ts;
  final Map<String, double> values;

  TimeSeriesPoint({required this.ts, required this.values});

  factory TimeSeriesPoint.fromJson(Map<String, dynamic> json) {
    final values = <String, double>{};
    json.forEach((key, value) {
      if (key != 'ts') {
        values[key] = (value ?? 0.0).toDouble();
      }
    });
    return TimeSeriesPoint(ts: json['ts'] ?? '', values: values);
  }
}

class HistogramData {
  final List<double> edges;
  final List<int> counts;

  HistogramData({required this.edges, required this.counts});

  factory HistogramData.fromJson(Map<String, dynamic> json) {
    return HistogramData(
      edges: (json['edges'] as List? ?? []).map((e) => (e ?? 0.0).toDouble()).toList(),
      counts: (json['counts'] as List? ?? []).map((e) => e as int).toList(),
    );
  }
}

class ModelRunSummary {
  final String modelRunId;
  final String datasetId;
  final String name;
  final String status;
  final String artifactPath;
  final String createdAt;
  final String completedAt;

  ModelRunSummary({
    required this.modelRunId,
    required this.datasetId,
    required this.name,
    required this.status,
    required this.artifactPath,
    required this.createdAt,
    required this.completedAt,
  });

  factory ModelRunSummary.fromJson(Map<String, dynamic> json) {
    return ModelRunSummary(
      modelRunId: json['model_run_id'] ?? '',
      datasetId: json['dataset_id'] ?? '',
      name: json['name'] ?? '',
      status: json['status'] ?? '',
      artifactPath: json['artifact_path'] ?? '',
      createdAt: json['created_at'] ?? '',
      completedAt: json['completed_at'] ?? '',
    );
  }
}

class InferenceRunSummary {
  final String inferenceId;
  final String modelRunId;
  final String datasetId;
  final String status;
  final int anomalyCount;
  final double latencyMs;
  final String createdAt;

  InferenceRunSummary({
    required this.inferenceId,
    required this.modelRunId,
    required this.datasetId,
    required this.status,
    required this.anomalyCount,
    required this.latencyMs,
    required this.createdAt,
  });

  factory InferenceRunSummary.fromJson(Map<String, dynamic> json) {
    return InferenceRunSummary(
      inferenceId: json['inference_id'] ?? '',
      modelRunId: json['model_run_id'] ?? '',
      datasetId: json['dataset_id'] ?? '',
      status: json['status'] ?? '',
      anomalyCount: json['anomaly_count'] ?? 0,
      latencyMs: (json['latency_ms'] ?? 0.0).toDouble(),
      createdAt: json['created_at'] ?? '',
    );
  }
}

class ScoreJobStatus {
  final String jobId;
  final String status;
  final int totalRows;
  final int processedRows;
  final String error;

  ScoreJobStatus({
    required this.jobId,
    required this.status,
    required this.totalRows,
    required this.processedRows,
    required this.error,
  });

  factory ScoreJobStatus.fromJson(Map<String, dynamic> json) {
    return ScoreJobStatus(
      jobId: json['job_id'] ?? '',
      status: json['status'] ?? '',
      totalRows: json['total_rows'] ?? 0,
      processedRows: json['processed_rows'] ?? 0,
      error: json['error'] ?? '',
    );
  }
}

class EvalMetrics {
  final Map<String, int> confusion;
  final List<Map<String, double>> roc;
  final List<Map<String, double>> pr;

  EvalMetrics({
    required this.confusion,
    required this.roc,
    required this.pr,
  });

  factory EvalMetrics.fromJson(Map<String, dynamic> json) {
    return EvalMetrics(
      confusion: {
        'tp': json['confusion']?['tp'] ?? 0,
        'fp': json['confusion']?['fp'] ?? 0,
        'tn': json['confusion']?['tn'] ?? 0,
        'fn': json['confusion']?['fn'] ?? 0,
      },
      roc: (json['roc'] as List? ?? [])
          .map((e) => (e as Map).map((k, v) => MapEntry(k.toString(), (v ?? 0.0).toDouble())))
          .toList(),
      pr: (json['pr'] as List? ?? [])
          .map((e) => (e as Map).map((k, v) => MapEntry(k.toString(), (v ?? 0.0).toDouble())))
          .toList(),
    );
  }
}

class ErrorDistributionEntry {
  final String label;
  final int count;
  final double mean;
  final double p50;
  final double p95;

  ErrorDistributionEntry({
    required this.label,
    required this.count,
    required this.mean,
    required this.p50,
    required this.p95,
  });

  factory ErrorDistributionEntry.fromJson(Map<String, dynamic> json) {
    return ErrorDistributionEntry(
      label: json['label'] ?? '',
      count: json['count'] ?? 0,
      mean: (json['mean'] ?? 0.0).toDouble(),
      p50: (json['p50'] ?? 0.0).toDouble(),
      p95: (json['p95'] ?? 0.0).toDouble(),
    );
  }
}

class _CacheEntry {
  final DateTime insertedAt;
  final dynamic value;

  _CacheEntry(this.insertedAt, this.value);
}

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
  final Duration cacheTtl = const Duration(seconds: 30);
  final Map<String, _CacheEntry> _cache = {};

  String _cacheKey(String path, [Map<String, String>? params]) {
    if (params == null || params.isEmpty) return path;
    final entries = params.entries.toList()
      ..sort((a, b) => a.key.compareTo(b.key));
    return '$path?${entries.map((e) => '${e.key}=${e.value}').join('&')}';
  }

  T? _readCache<T>(String key) {
    final entry = _cache[key];
    if (entry == null) return null;
    if (DateTime.now().difference(entry.insertedAt) > cacheTtl) {
      _cache.remove(key);
      return null;
    }
    return entry.value as T;
  }

  void _writeCache(String key, dynamic value) {
    _cache[key] = _CacheEntry(DateTime.now(), value);
  }

  Uri _buildUri(String path, [Map<String, String>? params]) {
    return Uri.parse('$baseUrl$path').replace(queryParameters: params);
  }

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

  Future<List<DatasetRun>> listDatasets({int limit = 50, int offset = 0}) async {
    final params = {'limit': '$limit', 'offset': '$offset'};
    final key = _cacheKey('/datasets', params);
    final cached = _readCache<List<DatasetRun>>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/datasets', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => DatasetRun.fromJson(e as Map<String, dynamic>))
          .toList();
      _writeCache(key, items);
      return items;
    }
    throw Exception('Failed to list datasets: ${response.body}');
  }

  Future<Map<String, dynamic>> getDatasetDetail(String runId) async {
    final key = _cacheKey('/datasets/$runId');
    final cached = _readCache<Map<String, dynamic>>(key);
    if (cached != null) return cached;
    final response = await http.get(Uri.parse('$baseUrl/datasets/$runId'));
    if (response.statusCode == 200) {
      final data = jsonDecode(response.body) as Map<String, dynamic>;
      _writeCache(key, data);
      return data;
    }
    throw Exception('Failed to get dataset detail: ${response.body}');
  }

  Future<DatasetSummary> getDatasetSummary(String runId, {int topk = 5}) async {
    final params = {'topk': '$topk'};
    final key = _cacheKey('/datasets/$runId/summary', params);
    final cached = _readCache<DatasetSummary>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/datasets/$runId/summary', params));
    if (response.statusCode == 200) {
      final summary = DatasetSummary.fromJson(jsonDecode(response.body));
      _writeCache(key, summary);
      return summary;
    }
    throw Exception('Failed to get dataset summary: ${response.body}');
  }

  Future<List<TopKEntry>> getTopK(String runId, String column,
      {int k = 10,
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime}) async {
    final params = <String, String>{
      'column': column,
      'k': '$k',
    };
    if (isAnomaly != null) params['is_anomaly'] = isAnomaly;
    if (anomalyType != null) params['anomaly_type'] = anomalyType;
    if (startTime != null) params['start_time'] = startTime;
    if (endTime != null) params['end_time'] = endTime;
    final key = _cacheKey('/datasets/$runId/topk', params);
    final cached = _readCache<List<TopKEntry>>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/datasets/$runId/topk', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => TopKEntry.fromJson(e as Map<String, dynamic>))
          .toList();
      _writeCache(key, items);
      return items;
    }
    throw Exception('Failed to get topk: ${response.body}');
  }

  Future<List<TimeSeriesPoint>> getTimeSeries(String runId,
      {required List<String> metrics,
      List<String> aggs = const ['mean'],
      String bucket = '1h',
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime}) async {
    final params = <String, String>{
      'metrics': metrics.join(','),
      'aggs': aggs.join(','),
      'bucket': bucket,
    };
    if (isAnomaly != null) params['is_anomaly'] = isAnomaly;
    if (anomalyType != null) params['anomaly_type'] = anomalyType;
    if (startTime != null) params['start_time'] = startTime;
    if (endTime != null) params['end_time'] = endTime;
    final key = _cacheKey('/datasets/$runId/timeseries', params);
    final cached = _readCache<List<TimeSeriesPoint>>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/datasets/$runId/timeseries', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => TimeSeriesPoint.fromJson(e as Map<String, dynamic>))
          .toList();
      _writeCache(key, items);
      return items;
    }
    throw Exception('Failed to get timeseries: ${response.body}');
  }

  Future<HistogramData> getHistogram(String runId,
      {required String metric,
      int bins = 40,
      String range = 'minmax',
      double? min,
      double? max,
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime}) async {
    final params = <String, String>{
      'metric': metric,
      'bins': '$bins',
      'range': range,
    };
    if (min != null) params['min'] = '$min';
    if (max != null) params['max'] = '$max';
    if (isAnomaly != null) params['is_anomaly'] = isAnomaly;
    if (anomalyType != null) params['anomaly_type'] = anomalyType;
    if (startTime != null) params['start_time'] = startTime;
    if (endTime != null) params['end_time'] = endTime;
    final key = _cacheKey('/datasets/$runId/histogram', params);
    final cached = _readCache<HistogramData>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/datasets/$runId/histogram', params));
    if (response.statusCode == 200) {
      final data = HistogramData.fromJson(jsonDecode(response.body));
      _writeCache(key, data);
      return data;
    }
    throw Exception('Failed to get histogram: ${response.body}');
  }

  Future<List<ModelRunSummary>> listModels({int limit = 50, int offset = 0}) async {
    final params = {'limit': '$limit', 'offset': '$offset'};
    final key = _cacheKey('/models', params);
    final cached = _readCache<List<ModelRunSummary>>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/models', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => ModelRunSummary.fromJson(e as Map<String, dynamic>))
          .toList();
      _writeCache(key, items);
      return items;
    }
    throw Exception('Failed to list models: ${response.body}');
  }

  Future<Map<String, dynamic>> getModelDetail(String modelRunId) async {
    final key = _cacheKey('/models/$modelRunId');
    final cached = _readCache<Map<String, dynamic>>(key);
    if (cached != null) return cached;
    final response = await http.get(Uri.parse('$baseUrl/models/$modelRunId'));
    if (response.statusCode == 200) {
      final data = jsonDecode(response.body) as Map<String, dynamic>;
      _writeCache(key, data);
      return data;
    }
    throw Exception('Failed to get model detail: ${response.body}');
  }

  Future<List<InferenceRunSummary>> listInferenceRuns(
      {String? datasetId, String? modelRunId, int limit = 50, int offset = 0}) async {
    final params = <String, String>{
      'limit': '$limit',
      'offset': '$offset',
    };
    if (datasetId != null) params['dataset_id'] = datasetId;
    if (modelRunId != null) params['model_run_id'] = modelRunId;
    final key = _cacheKey('/inference_runs', params);
    final cached = _readCache<List<InferenceRunSummary>>(key);
    if (cached != null) return cached;
    final response = await http.get(_buildUri('/inference_runs', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => InferenceRunSummary.fromJson(e as Map<String, dynamic>))
          .toList();
      _writeCache(key, items);
      return items;
    }
    throw Exception('Failed to list inference runs: ${response.body}');
  }

  Future<Map<String, dynamic>> getInferenceRun(String inferenceId) async {
    final response = await http.get(Uri.parse('$baseUrl/inference_runs/$inferenceId'));
    if (response.statusCode == 200) {
      return jsonDecode(response.body) as Map<String, dynamic>;
    }
    throw Exception('Failed to get inference run: ${response.body}');
  }

  Future<String> startScoreJob(String datasetId, String modelRunId) async {
    final response = await http.post(
      Uri.parse('$baseUrl/jobs/score_dataset'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'dataset_id': datasetId, 'model_run_id': modelRunId}),
    );
    if (response.statusCode == 200 || response.statusCode == 202) {
      return jsonDecode(response.body)['job_id'];
    }
    throw Exception('Failed to start score job: ${response.body}');
  }

  Future<ScoreJobStatus> getJobStatus(String jobId) async {
    final response = await http.get(Uri.parse('$baseUrl/jobs/$jobId'));
    if (response.statusCode == 200) {
      return ScoreJobStatus.fromJson(jsonDecode(response.body));
    }
    throw Exception('Failed to get job status: ${response.body}');
  }

  Future<EvalMetrics> getModelEval(String modelRunId, String datasetId,
      {int points = 50, int maxSamples = 20000}) async {
    final params = {
      'dataset_id': datasetId,
      'points': '$points',
      'max_samples': '$maxSamples',
    };
    final response = await http.get(_buildUri('/models/$modelRunId/eval', params));
    if (response.statusCode == 200) {
      return EvalMetrics.fromJson(jsonDecode(response.body));
    }
    throw Exception('Failed to get eval metrics: ${response.body}');
  }

  Future<List<ErrorDistributionEntry>> getErrorDistribution(
      String modelRunId, String datasetId,
      {required String groupBy}) async {
    final params = {
      'dataset_id': datasetId,
      'group_by': groupBy,
    };
    final response = await http.get(_buildUri('/models/$modelRunId/error_distribution', params));
    if (response.statusCode == 200) {
      final items = (jsonDecode(response.body)['items'] as List? ?? [])
          .map((e) => ErrorDistributionEntry.fromJson(e as Map<String, dynamic>))
          .toList();
      return items;
    }
    throw Exception('Failed to get error distribution: ${response.body}');
  }
}
