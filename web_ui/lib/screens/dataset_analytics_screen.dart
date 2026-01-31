import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/charts.dart';

class DatasetAnalyticsScreen extends StatefulWidget {
  const DatasetAnalyticsScreen({super.key});

  @override
  State<DatasetAnalyticsScreen> createState() => _DatasetAnalyticsScreenState();
}

class _DatasetAnalyticsScreenState extends State<DatasetAnalyticsScreen> {
  Future<DatasetSummary>? _summaryFuture;
  Future<List<TopKEntry>>? _topRegionsFuture;
  Future<List<TopKEntry>>? _topAnomalyFuture;
  Future<HistogramData>? _metricHistFuture;
  Future<List<TimeSeriesPoint>>? _metricTsFuture;

  List<Map<String, String>> _availableMetrics = [];
  bool _loadingSchema = false;

  @override
  void initState() {
    super.initState();
    _fetchSchema();
  }

  Future<void> _fetchSchema() async {
    setState(() => _loadingSchema = true);
    try {
      final schema = await context.read<TelemetryService>().getMetricsSchema();
      setState(() => _availableMetrics = schema);
    } catch (_) {
      // Fallback if schema API fails
      setState(() {
        _availableMetrics = [
          {'key': 'cpu_usage', 'label': 'CPU Usage'},
          {'key': 'memory_usage', 'label': 'Memory Usage'},
          {'key': 'disk_utilization', 'label': 'Disk Utilization'},
          {'key': 'network_rx_rate', 'label': 'Network RX Rate'},
          {'key': 'network_tx_rate', 'label': 'Network TX Rate'},
        ];
      });
    } finally {
      setState(() => _loadingSchema = false);
    }
  }

  void _load(String datasetId, String metric) {
    final service = context.read<TelemetryService>();
    _summaryFuture = service.getDatasetSummary(datasetId);
    _topRegionsFuture = service.getTopK(datasetId, 'region', k: 10);
    _topAnomalyFuture = service.getTopK(datasetId, 'anomaly_type', k: 10, isAnomaly: 'true');
    _metricHistFuture = service.getHistogram(datasetId, metric: metric, bins: 30);
    _metricTsFuture =
        service.getTimeSeries(datasetId, metrics: [metric], aggs: ['mean'], bucket: '1h');
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final datasetId = appState.datasetId;
    if (datasetId == null) {
      return const Center(child: Text('Select a dataset run to view analytics.'));
    }

    final selectedMetric = appState.getSelectedMetric(datasetId);

    if (_summaryFuture == null) {
      _load(datasetId, selectedMetric);
    }

    return Padding(
      padding: const EdgeInsets.all(24),
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Dataset Analytics — $datasetId',
                        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        const Text('Metric: ', style: TextStyle(color: Colors.white60)),
                        const SizedBox(width: 8),
                        DropdownButton<String>(
                          value: selectedMetric,
                          dropdownColor: const Color(0xFF1E293B),
                          items: _availableMetrics.map((m) {
                            return DropdownMenuItem(
                              value: m['key'],
                              child: Text(m['label']!),
                            );
                          }).toList(),
                          onChanged: (val) {
                            if (val != null) {
                              appState.setSelectedMetric(datasetId, val);
                              setState(() {
                                _load(datasetId, val);
                              });
                            }
                          },
                        ),
                      ],
                    ),
                  ],
                ),
                IconButton(
                  onPressed: () {
                    setState(() {
                      _load(datasetId, selectedMetric);
                    });
                  },
                  icon: const Icon(Icons.refresh),
                ),
              ],
            ),
            const SizedBox(height: 16),
            FutureBuilder<DatasetSummary>(
              future: _summaryFuture,
              builder: (context, snapshot) {
                if (snapshot.connectionState == ConnectionState.waiting) {
                  return const LinearProgressIndicator();
                }
                if (snapshot.hasError) {
                  return Text('Error: ${snapshot.error}');
                }
                final summary = snapshot.data;
                if (summary == null) return const SizedBox.shrink();
                return Wrap(
                  spacing: 16,
                  runSpacing: 16,
                  children: [
                    _statCard('Rows', '${summary.rowCount}'),
                    _statCard('Anomaly Rate', summary.anomalyRate.toStringAsFixed(4)),
                    _statCard('Hosts', '${summary.distinctCounts['host_id']}'),
                    _statCard('Projects', '${summary.distinctCounts['project_id']}'),
                    _statCard('Regions', '${summary.distinctCounts['region']}'),
                    _statCard('Ingestion p50 (s)', summary.ingestionLatencyP50.toStringAsFixed(3)),
                    _statCard('Ingestion p95 (s)', summary.ingestionLatencyP95.toStringAsFixed(3)),
                    _statCard('Time Range', '${summary.minTs} → ${summary.maxTs}'),
                  ],
                );
              },
            ),
            const SizedBox(height: 24),
            Wrap(
              spacing: 16,
              runSpacing: 16,
              children: [
                SizedBox(
                  width: 420,
                  child: ChartCard(
                    title: 'Top Regions',
                    child: FutureBuilder<List<TopKEntry>>(
                      future: _topRegionsFuture,
                      builder: (context, snapshot) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const Center(child: CircularProgressIndicator());
                        }
                        if (snapshot.hasError) {
                          return Text('Error: ${snapshot.error}');
                        }
                        final items = snapshot.data ?? [];
                        if (items.isEmpty) return const SizedBox.shrink();
                        return BarChart(values: items.map((e) => e.count.toDouble()).toList());
                      },
                    ),
                  ),
                ),
                SizedBox(
                  width: 420,
                  child: ChartCard(
                    title: 'Anomaly Types',
                    child: FutureBuilder<List<TopKEntry>>(
                      future: _topAnomalyFuture,
                      builder: (context, snapshot) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const Center(child: CircularProgressIndicator());
                        }
                        if (snapshot.hasError) {
                          return Text('Error: ${snapshot.error}');
                        }
                        final items = snapshot.data ?? [];
                        if (items.isEmpty) return const SizedBox.shrink();
                        return BarChart(values: items.map((e) => e.count.toDouble()).toList());
                      },
                    ),
                  ),
                ),
                SizedBox(
                  width: 420,
                  child: ChartCard(
                    title: '$selectedMetric Histogram',
                    child: FutureBuilder<HistogramData>(
                      future: _metricHistFuture,
                      builder: (context, snapshot) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const Center(child: CircularProgressIndicator());
                        }
                        if (snapshot.hasError) {
                          return Text('Error: ${snapshot.error}');
                        }
                        final hist = snapshot.data;
                        final values = hist?.counts.map((e) => e.toDouble()).toList() ?? [];
                        if (values.isEmpty) return const SizedBox.shrink();
                        return BarChart(values: values);
                      },
                    ),
                  ),
                ),
                SizedBox(
                  width: 420,
                  child: ChartCard(
                    title: 'Anomaly Rate Trend (1h)',
                    child: FutureBuilder<DatasetSummary>(
                      future: _summaryFuture,
                      builder: (context, snapshot) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const Center(child: CircularProgressIndicator());
                        }
                        if (snapshot.hasError) {
                          return Text('Error: ${snapshot.error}');
                        }
                        final trend = snapshot.data?.anomalyRateTrend ?? [];
                        if (trend.isEmpty) return const SizedBox.shrink();
                        final xs = List<double>.generate(trend.length, (i) => i.toDouble());
                        final ys = trend.map((e) => e.anomalyRate).toList();
                        return LineChart(x: xs, y: ys);
                      },
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),
            SizedBox(
              width: double.infinity,
              child: ChartCard(
                title: '$selectedMetric Mean (1h)',
                height: 240,
                child: FutureBuilder<List<TimeSeriesPoint>>(
                  future: _metricTsFuture,
                  builder: (context, snapshot) {
                    if (snapshot.connectionState == ConnectionState.waiting) {
                      return const Center(child: CircularProgressIndicator());
                    }
                    if (snapshot.hasError) {
                      return Text('Error: ${snapshot.error}');
                    }
                    final points = snapshot.data ?? [];
                    if (points.isEmpty) return const SizedBox.shrink();
                    final xs = List<double>.generate(points.length, (i) => i.toDouble());
                    final ys = points.map((e) => e.values['${selectedMetric}_mean'] ?? 0.0).toList();
                    return LineChart(x: xs, y: ys);
                  },
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _statCard(String label, String value) {
    return Container(
      width: 220,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.2),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: Colors.white60, fontSize: 12)),
          const SizedBox(height: 6),
          Text(value, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}
