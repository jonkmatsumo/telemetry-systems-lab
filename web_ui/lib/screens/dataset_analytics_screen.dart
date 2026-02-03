import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/charts.dart';
import '../widgets/inline_alert.dart';

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

  Future<HistogramData>? _comparisonHistFuture;
  Future<List<TimeSeriesPoint>>? _comparisonTsFuture;

  List<Map<String, String>> _availableMetrics = [];
  bool _loadingSchema = false;
  String? _loadError;
  String? _schemaError;
  String? _comparisonMetric;
  Map<String, dynamic>? _metricsSummary;
  
  String? _loadedMetric;
  DateTimeRange? _timeRange;

  @override
  void initState() {
    super.initState();
    _fetchSchema();
  }

  String _iso(DateTime dt) => dt.toIso8601String();

  void _load(String datasetId, String metric) {
    _loadedMetric = metric;
    final service = context.read<TelemetryService>();
    _loadError = null;
    
    String? start, end;
    if (_timeRange != null) {
      start = _iso(_timeRange!.start);
      end = _iso(_timeRange!.end);
    }

    _summaryFuture = service.getDatasetSummary(datasetId).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Failed to load summary: $e');
      });
      throw e;
    });
    _topRegionsFuture = service.getTopK(datasetId, 'region', k: 10, startTime: start, endTime: end);
    _topAnomalyFuture = service.getTopK(datasetId, 'anomaly_type', k: 10, isAnomaly: 'true', startTime: start, endTime: end);
    _metricHistFuture = service.getHistogram(datasetId, metric: metric, bins: 30, startTime: start, endTime: end).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Metric "$metric" is not supported.');
      });
      throw e;
    });
    _metricTsFuture =
        service.getTimeSeries(datasetId, metrics: [metric], aggs: ['mean'], bucket: '1h', startTime: start, endTime: end).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Metric "$metric" is not supported.');
      });
      throw e;
    });

    if (_comparisonMetric != null) {
      _comparisonHistFuture = service
          .getHistogram(datasetId, metric: _comparisonMetric!, bins: 30, startTime: start, endTime: end)
          .catchError((e) {
        debugPrint('Failed to load comparison histogram: $e');
        return HistogramData(edges: const [], counts: const []);
      });
      _comparisonTsFuture = service
          .getTimeSeries(datasetId, metrics: [_comparisonMetric!], aggs: ['mean'], bucket: '1h', startTime: start, endTime: end)
          .catchError((e) {
        debugPrint('Failed to load comparison time series: $e');
        return <TimeSeriesPoint>[];
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final datasetId = appState.datasetId;
    if (datasetId == null) {
      return const Center(child: Text('Select a dataset run to view analytics.'));
    }

    final selectedMetric = appState.getSelectedMetric(datasetId);

    if (_summaryFuture == null || _loadedMetric != selectedMetric) {
      _load(datasetId, selectedMetric);
    }

    return Padding(
      padding: const EdgeInsets.all(24),
      child: DefaultTabController(
        length: 2,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('Dataset Analytics — $datasetId',
                    style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                const TabBar(
                  isScrollable: true,
                  labelColor: Color(0xFF38BDF8),
                  unselectedLabelColor: Colors.white60,
                  indicatorColor: Color(0xFF38BDF8),
                  tabs: [
                    Tab(text: 'Dashboard'),
                    Tab(text: 'Distributions'),
                  ],
                ),
                Row(
                  children: [
                    OutlinedButton.icon(
                      onPressed: () async {
                         final picked = await showDateRangePicker(
                           context: context,
                           firstDate: DateTime(2023),
                           lastDate: DateTime.now().add(const Duration(days: 1)),
                           initialDateRange: _timeRange,
                         );
                         if (picked != null) {
                           setState(() {
                             _timeRange = picked;
                             _load(datasetId, selectedMetric);
                           });
                         }
                      },
                      icon: const Icon(Icons.date_range),
                      label: Text(_timeRange == null ? 'All Time' : '${_timeRange!.start.month}/${_timeRange!.start.day} - ${_timeRange!.end.month}/${_timeRange!.end.day}'),
                    ),
                    const SizedBox(width: 8),
                    IconButton(
                      onPressed: () {
                        setState(() {
                          _load(datasetId, selectedMetric);
                        });
                      },
                      icon: const Icon(Icons.refresh),
                    ),
                    OutlinedButton.icon(
                      onPressed: () {
                        final uri = Uri.base;
                        final link = uri.replace(queryParameters: {
                          ...uri.queryParameters,
                          'datasetId': datasetId,
                          'metric': selectedMetric,
                        }).toString();
                        Clipboard.setData(ClipboardData(text: link));
                        ScaffoldMessenger.of(context)
                            .showSnackBar(const SnackBar(content: Text('Link copied to clipboard')));
                      },
                      icon: const Icon(Icons.link),
                      label: const Text('Copy Link'),
                    ),
                  ],
                ),
              ],
            ),
            if (_schemaError != null) ...[
              InlineAlert(
                message: _schemaError!,
                onRetry: _fetchSchema,
              ),
              const SizedBox(height: 16),
            ],
            const SizedBox(height: 16),
            Expanded(
              child: TabBarView(
                children: [
                  _buildDashboard(datasetId, selectedMetric, appState),
                  _buildDistributions(datasetId, selectedMetric, appState),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildDashboard(String datasetId, String selectedMetric, AppState appState) {
    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Text('Quick Metric Toggle: ', style: TextStyle(color: Colors.white60)),
              const SizedBox(width: 8),
              if (_loadingSchema)
                const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              else
                DropdownButton<String>(
                  value: _availableMetrics.any((m) => m['key'] == selectedMetric)
                      ? selectedMetric
                      : (_availableMetrics.isNotEmpty ? _availableMetrics.first['key'] : null),
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
          if (_loadError != null) ...[
            const SizedBox(height: 16),
            InlineAlert(
              message: _loadError!,
              onRetry: () {
                setState(() {
                  _load(datasetId, selectedMetric);
                });
              },
            ),
          ],
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
                      return BarChart(
                        values: items.map((e) => e.count.toDouble()).toList(),
                        labels: items.map((e) => e.label).toList(),
                      );
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
                      return BarChart(
                        values: items.map((e) => e.count.toDouble()).toList(),
                        labels: items.map((e) => e.label).toList(),
                      );
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
                        return const SizedBox.shrink();
                      }
                      final hist = snapshot.data;
                      final values = hist?.counts.map((e) => e.toDouble()).toList() ?? [];
                      if (values.isEmpty) return const SizedBox.shrink();
                      final labels = List.generate(values.length, (i) {
                         if (i < hist!.edges.length) {
                           return hist.edges[i].toStringAsFixed(1);
                         }
                         return "";
                      });
                      return BarChart(values: values, labels: labels);
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
                        return const SizedBox.shrink();
                      }
                      final trend = snapshot.data?.anomalyRateTrend ?? [];
                      if (trend.isEmpty) return const SizedBox.shrink();
                      final xs = List<double>.generate(trend.length, (i) => i.toDouble());
                      final ys = trend.map((e) => e.anomalyRate).toList();
                      return LineChart(
                        x: xs, 
                        y: ys,
                        xLabelBuilder: (val) {
                          int idx = val.round();
                          if (idx >= 0 && idx < trend.length) {
                             try {
                               final dt = DateTime.parse(trend[idx].ts).toLocal();
                               return "${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}";
                             } catch (_) {}
                          }
                          return "";
                        },
                        yLabelBuilder: (val) => val.toStringAsFixed(2),
                      );
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
                    return const SizedBox.shrink();
                  }
                  final points = snapshot.data ?? [];
                  if (points.isEmpty) return const SizedBox.shrink();
                  final xs = List<double>.generate(points.length, (i) => i.toDouble());
                  final ys = points.map((e) => e.values['${selectedMetric}_mean'] ?? 0.0).toList();
                  
                  final maxCount = points.isEmpty ? 0 : points.map((e) => e.count).reduce((a, b) => a > b ? a : b);
                  final partial = points.map((e) => e.count < maxCount * 0.9).toList();

                  return LineChart(
                    x: xs, 
                    y: ys,
                    partial: partial,
                    xLabelBuilder: (val) {
                      int idx = val.round();
                      if (idx >= 0 && idx < points.length) {
                         try {
                           final dt = DateTime.parse(points[idx].ts).toLocal();
                           return "${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}";
                         } catch (_) {}
                      }
                      return "";
                    },
                    yLabelBuilder: (val) => val.toStringAsFixed(1),
                  );
                },
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDistributions(String datasetId, String selectedMetric, AppState appState) {
    return Row(
      children: [
        Container(
          width: 200,
          decoration: const BoxDecoration(
            border: Border(right: BorderSide(color: Colors.white12)),
          ),
          child: Column(
            children: [
              if (_metricsSummary != null) _buildSuggestions(datasetId, appState),
              const Divider(height: 1),
              Expanded(
                child: ListView.builder(
                  itemCount: _availableMetrics.length,
                  itemBuilder: (context, index) {
                    final m = _availableMetrics[index];
                    final isPrimary = m['key'] == selectedMetric;
                    final isSecondary = m['key'] == _comparisonMetric;
                    return ListTile(
                      title: Text(m['label']!,
                          style: TextStyle(
                              fontSize: 13,
                              color: isPrimary
                                  ? const Color(0xFF38BDF8)
                                  : (isSecondary ? const Color(0xFF818CF8) : Colors.white70))),
                      onTap: () {
                        appState.setSelectedMetric(datasetId, m['key']!);
                        setState(() {
                          _load(datasetId, m['key']!);
                        });
                      },
                      trailing: isPrimary
                          ? null
                          : IconButton(
                              icon: Icon(isSecondary ? Icons.compare_arrows : Icons.add_circle_outline,
                                  size: 16,
                                  color: isSecondary ? const Color(0xFF818CF8) : Colors.white24),
                              onPressed: () {
                                setState(() {
                                  if (isSecondary) {
                                    _comparisonMetric = null;
                                  } else {
                                    _comparisonMetric = m['key'];
                                  }
                                  _load(datasetId, selectedMetric);
                                });
                              },
                            ),
                      selected: isPrimary,
                      dense: true,
                    );
                  },
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(child: _buildQuickStats(datasetId, selectedMetric)),
                    if (_comparisonMetric != null) ...[
                      const SizedBox(width: 16),
                      Expanded(
                          child: _buildQuickStats(datasetId, _comparisonMetric!,
                              color: const Color(0xFF818CF8))),
                    ],
                  ],
                ),
                const SizedBox(height: 24),
                _buildChartRow('Distribution (Histogram)', selectedMetric, _metricHistFuture,
                    _comparisonMetric, _comparisonHistFuture, true),
                const SizedBox(height: 24),
                _buildChartRow('Trend (1h Mean)', selectedMetric, _metricTsFuture, _comparisonMetric,
                    _comparisonTsFuture, false),
              ],
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildSuggestions(String datasetId, AppState appState) {
    final highVar = _metricsSummary!['high_variance'] as List? ?? [];
    if (highVar.isEmpty) return const SizedBox.shrink();
    return Padding(
      padding: const EdgeInsets.all(8.0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Suggested (High Var)',
              style: TextStyle(fontSize: 11, color: Colors.white54, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          Wrap(
            spacing: 4,
            runSpacing: 4,
            children: highVar.take(3).map((m) {
              return ActionChip(
                label: Text(m['key'], style: const TextStyle(fontSize: 10)),
                padding: EdgeInsets.zero,
                backgroundColor: Colors.white10,
                onPressed: () {
                  appState.setSelectedMetric(datasetId, m['key']);
                  setState(() => _load(datasetId, m['key']));
                },
              );
            }).toList(),
          ),
        ],
      ),
    );
  }

  Widget _buildChartRow(String title, String m1, Future? f1, String? m2, Future? f2, bool isHist) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: ChartCard(
                title: m1,
                child: _buildChart(f1, isHist, m1),
              ),
            ),
            if (m2 != null) ...[
              const SizedBox(width: 16),
              Expanded(
                child: ChartCard(
                  title: m2,
                  child: _buildChart(f2, isHist, m2, color: const Color(0xFF818CF8)),
                ),
              ),
            ],
          ],
        ),
      ],
    );
  }

  Widget _buildChart(Future? future, bool isHist, String metric, {Color? color}) {
    return FutureBuilder(
      future: future,
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Center(child: CircularProgressIndicator());
        }
        if (snapshot.hasError || snapshot.data == null) {
          return const Center(child: Text('Failed to load'));
        }
        if (isHist) {
          final hist = snapshot.data as HistogramData;
          final values = hist.counts.map((e) => e.toDouble()).toList();
          final labels = List.generate(values.length, (i) {
             if (i < hist.edges.length) {
               return hist.edges[i].toStringAsFixed(1);
             }
             return "";
          });
          return BarChart(values: values, labels: labels, barColor: color ?? const Color(0xFF818CF8));
        } else {
          final points = snapshot.data as List<TimeSeriesPoint>;
          if (points.isEmpty) return const Center(child: Text('No data'));
          final xs = List<double>.generate(points.length, (i) => i.toDouble());
          final ys = points.map((e) => e.values['${metric}_mean'] ?? 0.0).toList();
          
          final maxCount = points.map((e) => e.count).reduce((a, b) => a > b ? a : b);
          final partial = points.map((e) => e.count < maxCount * 0.9).toList();

          return LineChart(
            x: xs, 
            y: ys, 
            lineColor: color ?? const Color(0xFF38BDF8),
            partial: partial,
            xLabelBuilder: (val) {
              int idx = val.round();
              if (idx >= 0 && idx < points.length) {
                 try {
                   final dt = DateTime.parse(points[idx].ts).toLocal();
                   return "${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}";
                 } catch (_) {}
              }
              return "";
            },
            yLabelBuilder: (val) => val.toStringAsFixed(1),
          );
        }
      },
    );
  }

  Widget _buildQuickStats(String datasetId, String metric, {Color color = const Color(0xFF38BDF8)}) {
    return FutureBuilder<Map<String, dynamic>>(
      future: context.read<TelemetryService>().getMetricStats(datasetId, metric),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const LinearProgressIndicator();
        }
        if (snapshot.hasError) {
          return const SizedBox.shrink();
        }
        final stats = snapshot.data!;
        return Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: color.withOpacity(0.05),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: color.withOpacity(0.2)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Summary Statistics for $metric',
                  style: TextStyle(fontWeight: FontWeight.bold, color: color)),
              const SizedBox(height: 12),
              Wrap(
                spacing: 24,
                runSpacing: 12,
                children: [
                  _statItem('Mean', (stats['mean'] as double).toStringAsFixed(3)),
                  _statItem('Min', (stats['min'] as double).toStringAsFixed(3)),
                  _statItem('Max', (stats['max'] as double).toStringAsFixed(3)),
                  _statItem('p50', (stats['p50'] as double).toStringAsFixed(3)),
                  _statItem('p95', (stats['p95'] as double).toStringAsFixed(3)),
                  _statItem('Count', stats['count'].toString()),
                ],
              ),
            ],
          ),
        );
      },
    );
  }

  Widget _statItem(String label, String value) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: const TextStyle(color: Colors.white54, fontSize: 12)),
        Text(value, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
      ],
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
