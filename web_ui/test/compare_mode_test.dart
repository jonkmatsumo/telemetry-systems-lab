import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/dataset_analytics_screen.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';

class _FakeTelemetryService extends TelemetryService {
  _FakeTelemetryService() : super(baseUrl: 'http://example.com');

  String? lastCompareMode;

  @override
  Future<List<Map<String, String>>> getMetricsSchema() async {
    return [
      {'key': 'cpu_usage', 'label': 'CPU Usage'},
    ];
  }

  @override
  Future<Map<String, dynamic>> getDatasetMetricsSummary(String runId) async {
    return {'high_variance': []};
  }

  @override
  Future<DatasetSummary> getDatasetSummary(String runId, {int topk = 5, bool forceRefresh = false}) async {
    return DatasetSummary.fromJson({
      'row_count': 10,
      'time_range': {
        'min_ts': '2026-02-03T00:00:00Z',
        'max_ts': '2026-02-03T01:00:00Z',
      },
      'anomaly_rate': 0.1,
      'distinct_counts': {'host_id': 1, 'project_id': 1, 'region': 1},
      'anomaly_type_counts': [],
      'ingestion_latency_p50': 0.1,
      'ingestion_latency_p95': 0.2,
      'anomaly_rate_trend': [
        {'ts': '2026-02-03T00:00:00Z', 'anomaly_rate': 0.1, 'total': 10}
      ],
      'meta': {'server_time': '2026-02-03T01:00:00Z'},
    });
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
    return TopKResponse(items: const [], meta: ResponseMeta(limit: 0, returned: 0, truncated: false, reason: ''));
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
    return HistogramData(
      edges: const [0.0, 1.0],
      counts: const [1, 2],
      meta: ResponseMeta(limit: 2, returned: 2, truncated: false, reason: 'histogram_bins'),
    );
  }

  @override
  Future<Map<String, dynamic>> getMetricStats(String runId, String metric) async {
    return {
      'mean': 0.5,
      'min': 0.1,
      'max': 0.9,
      'p50': 0.4,
      'p95': 0.8,
      'count': 10,
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
    lastCompareMode = compareMode;
    final items = [
      TimeSeriesPoint(ts: '2026-02-03T00:00:00Z', values: {'cpu_usage_mean': 0.5}, count: 10),
      TimeSeriesPoint(ts: '2026-02-03T01:00:00Z', values: {'cpu_usage_mean': 0.7}, count: 10),
    ];
    final baseline = compareMode == 'previous_period'
        ? [
            TimeSeriesPoint(ts: '2026-02-02T00:00:00Z', values: {'cpu_usage_mean': 0.4}, count: 10),
            TimeSeriesPoint(ts: '2026-02-02T01:00:00Z', values: {'cpu_usage_mean': 0.6}, count: 10),
          ]
        : <TimeSeriesPoint>[];
    return TimeSeriesResponse(
      items: items,
      baseline: baseline,
      meta: ResponseMeta(
        limit: 0,
        returned: items.length,
        truncated: false,
        reason: '',
        compareMode: compareMode,
        baselineStartTime: compareMode == 'previous_period' ? '2026-02-02T00:00:00Z' : null,
        baselineEndTime: compareMode == 'previous_period' ? '2026-02-03T00:00:00Z' : null,
      ),
      bucketSeconds: 3600,
    );
  }
}

void main() {
  testWidgets('Compare mode toggle requests previous period series', (tester) async {
    final service = _FakeTelemetryService();
    final appState = AppState()
      ..setDataset('ds-1')
      ..setFilterBucket('2026-02-03T00:00:00Z', '2026-02-03T02:00:00Z');

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => service),
          ChangeNotifierProvider(create: (_) => appState),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: DatasetAnalyticsScreen(),
          ),
        ),
      ),
    );

    await tester.pumpAndSettle();
    expect(find.byKey(const Key('compare-mode-switch')), findsOneWidget);

    await tester.ensureVisible(find.byKey(const Key('compare-mode-switch')));
    await tester.tap(find.byKey(const Key('compare-mode-switch')));
    await tester.pumpAndSettle();
    expect(service.lastCompareMode, 'previous_period');
    expect(find.textContaining('Baseline:'), findsOneWidget);
  });
}
