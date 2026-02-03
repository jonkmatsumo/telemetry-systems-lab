import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/dataset_analytics_screen.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';

class _FakeTelemetryService extends TelemetryService {
  _FakeTelemetryService() : super(baseUrl: 'http://example.com');

  @override
  Future<DatasetSummary> getDatasetSummary(String runId, {int topk = 5, bool forceRefresh = false}) async {
    return DatasetSummary.fromJson({
      'row_count': 0,
      'time_range': {'min_ts': '', 'max_ts': ''},
      'anomaly_rate': 0.0,
      'distinct_counts': {'host_id': 0, 'project_id': 0, 'region': 0},
      'anomaly_type_counts': [],
      'ingestion_latency_p50': 0,
      'ingestion_latency_p95': 0,
      'anomaly_rate_trend': [],
    });
  }

  @override
  Future<List<Map<String, String>>> getMetricsSchema() async {
    return [
      {'key': 'cpu_usage', 'label': 'CPU Usage'},
    ];
  }

  @override
  Future<Map<String, dynamic>> getDatasetMetricsSummary(String runId, {bool forceRefresh = false}) async {
    return {'high_variance': []};
  }

  @override
  Future<TopKResponse> getTopK(String runId, String column,
      {int k = 10,
      String? region,
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime,
      bool includeTotalDistinct = false,
      bool forceRefresh = false}) async {
    return TopKResponse(
      items: const [],
      meta: ResponseMeta(limit: k, returned: 0, truncated: false, reason: 'top_k_limit'),
    );
  }

  @override
  Future<HistogramData> getHistogram(String runId,
      {required String metric,
      int bins = 40,
      String range = 'minmax',
      double? min,
      double? max,
      String? region,
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime,
      bool forceRefresh = false}) async {
    return HistogramData(edges: const [], counts: const [], meta: ResponseMeta(limit: bins, returned: 0, truncated: false, reason: 'histogram_bins'));
  }

  @override
  Future<Map<String, dynamic>> getMetricStats(String runId, String metric) async {
    return {
      'mean': 0.0,
      'min': 0.0,
      'max': 0.0,
      'p50': 0.0,
      'p95': 0.0,
      'count': 0,
    };
  }

  @override
  Future<TimeSeriesResponse> getTimeSeries(String runId,
      {required List<String> metrics,
      List<String> aggs = const ['mean'],
      String bucket = '1h',
      String? region,
      String? isAnomaly,
      String? anomalyType,
      String? startTime,
      String? endTime,
      String? compareMode,
      bool forceRefresh = false}) async {
    return TimeSeriesResponse(items: const [], meta: ResponseMeta(limit: 0, returned: 0, truncated: false, reason: ''), bucketSeconds: 3600);
  }
}

void main() {
  testWidgets('Filter chips render when filters set', (tester) async {
    final appState = AppState()..setDataset('ds-1')..setSelectedMetric('ds-1', 'cpu_usage');
    appState.setFilterRegion('us-east');
    appState.setFilterAnomalyType('spike');

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => _FakeTelemetryService()),
          ChangeNotifierProvider(create: (_) => appState),
        ],
        child: const MaterialApp(
          home: Scaffold(body: DatasetAnalyticsScreen()),
        ),
      ),
    );

    await tester.pumpAndSettle();
    expect(find.text('region=us-east'), findsOneWidget);
    expect(find.text('type=spike'), findsOneWidget);
  });
}
