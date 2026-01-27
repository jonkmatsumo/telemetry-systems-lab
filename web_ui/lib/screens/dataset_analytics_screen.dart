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
  Future<HistogramData>? _cpuHistFuture;
  Future<List<TimeSeriesPoint>>? _cpuTsFuture;

  void _load(String datasetId) {
    final service = context.read<TelemetryService>();
    _summaryFuture = service.getDatasetSummary(datasetId);
    _topRegionsFuture = service.getTopK(datasetId, 'region', k: 10);
    _topAnomalyFuture = service.getTopK(datasetId, 'anomaly_type', k: 10, isAnomaly: 'true');
    _cpuHistFuture = service.getHistogram(datasetId, metric: 'cpu_usage', bins: 30);
    _cpuTsFuture = service.getTimeSeries(datasetId, metrics: ['cpu_usage'], aggs: ['mean'], bucket: '1h');
  }

  @override
  Widget build(BuildContext context) {
    final datasetId = context.watch<AppState>().datasetId;
    if (datasetId == null) {
      return const Center(child: Text('Select a dataset run to view analytics.'));
    }
    if (_summaryFuture == null) {
      _load(datasetId);
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
                Text('Dataset Analytics — $datasetId',
                    style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                IconButton(
                  onPressed: () {
                    setState(() {
                      _load(datasetId);
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
                        final items = snapshot.data ?? [];
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
                        final items = snapshot.data ?? [];
                        return BarChart(values: items.map((e) => e.count.toDouble()).toList());
                      },
                    ),
                  ),
                ),
                SizedBox(
                  width: 420,
                  child: ChartCard(
                    title: 'CPU Histogram',
                    child: FutureBuilder<HistogramData>(
                      future: _cpuHistFuture,
                      builder: (context, snapshot) {
                        final hist = snapshot.data;
                        final values = hist?.counts.map((e) => e.toDouble()).toList() ?? [];
                        return BarChart(values: values);
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
                title: 'CPU Usage Mean (1h)',
                height: 240,
                child: FutureBuilder<List<TimeSeriesPoint>>(
                  future: _cpuTsFuture,
                  builder: (context, snapshot) {
                    final points = snapshot.data ?? [];
                    if (points.isEmpty) return const SizedBox.shrink();
                    final xs = List<double>.generate(points.length, (i) => i.toDouble());
                    final ys = points.map((e) => e.values['cpu_usage_mean'] ?? 0.0).toList();
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
