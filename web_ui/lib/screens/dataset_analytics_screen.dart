import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../state/investigation_context.dart';
import '../widgets/analytics_state_panel.dart';
import '../widgets/charts.dart';
import '../widgets/inline_alert.dart';
import '../utils/freshness.dart';
import '../utils/time_buckets.dart';

class DatasetAnalyticsScreen extends StatefulWidget {
  const DatasetAnalyticsScreen({super.key});

  @override
  State<DatasetAnalyticsScreen> createState() => _DatasetAnalyticsScreenState();
}

class _DatasetAnalyticsScreenState extends State<DatasetAnalyticsScreen> {
  Future<DatasetSummary>? _summaryFuture;
  Future<TopKResponse>? _topRegionsFuture;
  Future<TopKResponse>? _topAnomalyFuture;
  Future<HistogramData>? _metricHistFuture;
  Future<HistogramData>? _metricHistAnomalyFuture;
  Future<TimeSeriesResponse>? _metricTsFuture;

  Future<HistogramData>? _comparisonHistFuture;
  Future<TimeSeriesResponse>? _comparisonTsFuture;

  List<Map<String, String>> _availableMetrics = [];
  bool _loadingSchema = false;
  String? _loadError;
  String? _schemaError;
  String? _comparisonMetric;
  Map<String, dynamic>? _metricsSummary;
  
  String? _loadedMetric;
  DateTimeRange? _timeRange;
  DateTime? _lastUpdated;
  bool _showAnomalyOverlay = false;
  String _bucketLabel = '1h';
  int _bucketSeconds = 3600;

  final Map<String, WidgetFreshness> _freshness = {};
  static const String _keySummary = 'summary';
  static const String _keyTopRegions = 'top_regions';
  static const String _keyTopAnomaly = 'top_anomaly';
  static const String _keyMetricHist = 'metric_hist';
  static const String _keyMetricHistAnomaly = 'metric_hist_anomaly';
  static const String _keyMetricTs = 'metric_ts';
  static const String _keyComparisonHist = 'comparison_hist';
  static const String _keyComparisonTs = 'comparison_ts';

  @override
  void initState() {
    super.initState();
    _fetchSchema();
  }

  Future<void> _fetchSchema() async {
    setState(() {
      _loadingSchema = true;
      _schemaError = null;
    });
    final service = context.read<TelemetryService>();
    try {
      final schema = await service.getMetricsSchema();
      if (!mounted) return;
      setState(() {
        _availableMetrics = schema;
        _loadingSchema = false;
      });

      final appState = context.read<AppState>();
      final datasetId = appState.datasetId;
      if (datasetId != null) {
        final summary = await service.getDatasetMetricsSummary(datasetId);
        if (mounted) setState(() => _metricsSummary = summary);

        final selectedMetric = appState.getSelectedMetric(datasetId);
        if (schema.isNotEmpty && !schema.any((m) => m['key'] == selectedMetric)) {
          final fallback = schema.first['key'];
          if (fallback != null) appState.setSelectedMetric(datasetId, fallback);
        }
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _schemaError = 'Failed to load metrics schema: $e';
        _loadingSchema = false;
      });
    }
  }

  String _iso(DateTime dt) => dt.toIso8601String();

  ResponseMeta _emptyMeta() {
    return ResponseMeta(limit: 0, returned: 0, truncated: false, reason: '');
  }

  HistogramData _emptyHistogram() {
    return HistogramData(edges: const [], counts: const [], meta: _emptyMeta());
  }

  TimeSeriesResponse _emptyTimeSeries() {
    return TimeSeriesResponse(items: const [], meta: _emptyMeta());
  }

  DateTime? _parseServerTime(String? raw) {
    if (raw == null || raw.isEmpty) return null;
    try {
      return DateTime.parse(raw);
    } catch (_) {
      return null;
    }
  }

  String _formatTime(DateTime dt, {required bool useUtc}) {
    final display = useUtc ? dt.toUtc() : dt.toLocal();
    final hh = display.hour.toString().padLeft(2, '0');
    final mm = display.minute.toString().padLeft(2, '0');
    final ss = display.second.toString().padLeft(2, '0');
    final tz = useUtc ? 'UTC' : 'Local';
    return '$hh:$mm:$ss $tz';
  }

  bool _shouldShowTick(int idx, int total) {
    if (total <= 12) return true;
    if (total <= 48) return idx % 4 == 0;
    if (total <= 120) return idx % 8 == 0;
    return idx % 12 == 0;
  }

  String? _buildCostInfo(ResponseMeta? meta) {
    if (meta == null) return null;
    final parts = <String>[];
    if (meta.durationMs != null) {
      parts.add('Duration ${meta.durationMs!.toStringAsFixed(1)} ms');
    }
    if (meta.rowsScanned != null) {
      parts.add('Scanned ${meta.rowsScanned}');
    }
    if (meta.rowsReturned != null) {
      parts.add('Returned ${meta.rowsReturned}');
    }
    if (meta.cacheHit != null) {
      parts.add(meta.cacheHit! ? 'Cache hit' : 'Cache miss');
    }
    if (parts.isEmpty) return null;
    return parts.join(' • ');
  }

  String? _asOfLabel(String key, {required bool useUtc}) {
    final freshness = _freshness[key];
    final time = freshness?.serverTime ?? freshness?.requestEnd;
    if (time == null) return null;
    return 'As of ${_formatTime(time, useUtc: useUtc)}';
  }

  void _setFreshness(String key, WidgetFreshness freshness) {
    if (!mounted) return;
    setState(() => _freshness[key] = freshness);
  }

  Future<T> _trackWidget<T>(String key, Future<T> future,
      {bool forceRefresh = false,
      String? startTime,
      String? endTime,
      DateTime? Function(T value)? serverTimeResolver}) {
    _setFreshness(
      key,
      WidgetFreshness(
        requestStart: DateTime.now(),
        forceRefresh: forceRefresh,
        startTime: startTime,
        endTime: endTime,
      ),
    );
    return future.then((value) {
      final serverTime = serverTimeResolver != null ? serverTimeResolver(value) : null;
      _setFreshness(
        key,
        _freshness[key]?.copyWith(
              requestEnd: DateTime.now(),
              serverTime: serverTime,
            ) ??
            WidgetFreshness(
              requestEnd: DateTime.now(),
              serverTime: serverTime,
              forceRefresh: forceRefresh,
              startTime: startTime,
              endTime: endTime,
            ),
      );
      return value;
    }).catchError((e) {
      _setFreshness(
        key,
        _freshness[key]?.copyWith(requestEnd: DateTime.now()) ??
            WidgetFreshness(requestEnd: DateTime.now(), forceRefresh: forceRefresh),
      );
      throw e;
    });
  }

  Widget? _buildFreshnessBanner(String datasetId, String metric) {
    if (!shouldShowFreshnessBanner(_freshness.values)) return null;
    final delta = maxFreshnessDelta(_freshness.values);
    final details = <String>[];
    if (delta != null && delta.inSeconds > 60) {
      details.add('Max delta ${delta.inSeconds}s');
    }
    if (hasMixedRefreshMode(_freshness.values)) {
      details.add('Mixed cache/refresh');
    }
    final detailText = details.isEmpty ? '' : ' (${details.join(' • ')})';
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Colors.orange.withOpacity(0.12),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.orange.withOpacity(0.3)),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber_rounded, color: Colors.orange, size: 20),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              'Widgets are out of sync$detailText',
              style: const TextStyle(color: Colors.white70, fontSize: 12),
            ),
          ),
          TextButton.icon(
            onPressed: () {
              _refreshAll(datasetId, metric);
            },
            icon: const Icon(Icons.refresh, size: 16, color: Colors.orange),
            label: const Text('Refresh all', style: TextStyle(color: Colors.orange)),
          ),
        ],
      ),
    );
  }

  void _load(String datasetId, String metric, {bool forceRefresh = false}) {
    _loadedMetric = metric;
    final service = context.read<TelemetryService>();
    final appState = context.read<AppState>();
    _loadError = null;
    
    String? start, end;
    if (appState.filterBucketStart != null || appState.filterBucketEnd != null) {
      start = appState.filterBucketStart;
      end = appState.filterBucketEnd;
    } else if (_timeRange != null) {
      start = _iso(_timeRange!.start);
      end = _iso(_timeRange!.end);
    }
    if (start != null && end != null) {
      final range = DateTime.parse(end).difference(DateTime.parse(start));
      _bucketLabel = selectBucketLabel(range);
      _bucketSeconds = bucketSecondsForLabel(_bucketLabel);
    } else {
      _bucketLabel = '1h';
      _bucketSeconds = 3600;
    }
    final regionFilter = appState.filterRegion;
    final anomalyFilter = appState.filterAnomalyType;

    _summaryFuture = _trackWidget(
      _keySummary,
      service.getDatasetSummary(datasetId, forceRefresh: forceRefresh),
      forceRefresh: forceRefresh,
      startTime: start,
      endTime: end,
      serverTimeResolver: (data) => _parseServerTime(data.serverTime),
    ).then((data) {
      if (mounted) setState(() => _lastUpdated = DateTime.now());
      return data;
    }).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Failed to load summary: $e');
      });
      throw e;
    });
    _topRegionsFuture = _trackWidget(
      _keyTopRegions,
      service.getTopK(datasetId, 'region', k: 10, region: regionFilter, anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
      forceRefresh: forceRefresh,
      startTime: start,
      endTime: end,
      serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
    );
    _topAnomalyFuture = _trackWidget(
      _keyTopAnomaly,
      service.getTopK(datasetId, 'anomaly_type', k: 10, region: regionFilter, isAnomaly: 'true', anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
      forceRefresh: forceRefresh,
      startTime: start,
      endTime: end,
      serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
    );
    
    _metricHistFuture = _trackWidget(
      _keyMetricHist,
      service.getHistogram(datasetId, metric: metric, bins: 30, region: regionFilter, anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
      forceRefresh: forceRefresh,
      startTime: start,
      endTime: end,
      serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
    ).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Metric "$metric" is not supported.');
      });
      throw e;
    });

    if (_showAnomalyOverlay) {
      _metricHistAnomalyFuture = _trackWidget(
        _keyMetricHistAnomaly,
        service.getHistogram(datasetId, metric: metric, bins: 30, region: regionFilter, isAnomaly: 'true', anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
        forceRefresh: forceRefresh,
        startTime: start,
        endTime: end,
        serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
      );
    } else {
      _metricHistAnomalyFuture = null;
    }

    _metricTsFuture = _trackWidget(
      _keyMetricTs,
      service.getTimeSeries(datasetId, metrics: [metric], aggs: ['mean'], bucket: _bucketLabel, region: regionFilter, anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
      forceRefresh: forceRefresh,
      startTime: start,
      endTime: end,
      serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
    ).catchError((e) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _loadError = 'Metric "$metric" is not supported.');
      });
      throw e;
    });

    if (_comparisonMetric != null) {
      _comparisonHistFuture = _trackWidget(
        _keyComparisonHist,
        service.getHistogram(datasetId, metric: _comparisonMetric!, bins: 30, region: regionFilter, anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
        forceRefresh: forceRefresh,
        startTime: start,
        endTime: end,
        serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
      ).catchError((e) {
        debugPrint('Failed to load comparison histogram: $e');
        return _emptyHistogram();
      });
      _comparisonTsFuture = _trackWidget(
        _keyComparisonTs,
        service.getTimeSeries(datasetId, metrics: [_comparisonMetric!], aggs: ['mean'], bucket: _bucketLabel, region: regionFilter, anomalyType: anomalyFilter, startTime: start, endTime: end, forceRefresh: forceRefresh),
        forceRefresh: forceRefresh,
        startTime: start,
        endTime: end,
        serverTimeResolver: (data) => _parseServerTime(data.meta.serverTime),
      ).catchError((e) {
        debugPrint('Failed to load comparison time series: $e');
        return _emptyTimeSeries();
      });
    }
  }

  void _refreshAll(String datasetId, String metric) {
    setState(() {
      _freshness.clear();
      _load(datasetId, metric, forceRefresh: true);
    });
  }

  InvestigationContext _buildContext({
    required String datasetId,
    required String metric,
    required bool useUtc,
    String? region,
    String? anomalyType,
    String? isAnomaly,
    String? startTime,
    String? endTime,
    String? bucketStart,
    String? bucketEnd,
    int? offset,
    int? limit,
  }) {
    return InvestigationContext(
      datasetId: datasetId,
      metric: metric,
      useUtc: useUtc,
      region: region,
      anomalyType: anomalyType,
      isAnomaly: isAnomaly,
      startTime: startTime,
      endTime: endTime,
      bucketStart: bucketStart,
      bucketEnd: bucketEnd,
      offset: offset,
      limit: limit,
    );
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final datasetId = appState.datasetId;
    final useUtc = appState.useUtc;
    if (datasetId == null) {
      return const Center(child: Text('Select a dataset run to view analytics.'));
    }

    final selectedMetric = appState.getSelectedMetric(datasetId);

    if (_summaryFuture == null || _loadedMetric != selectedMetric) {
      _load(datasetId, selectedMetric);
    }

    final freshnessBanner = _buildFreshnessBanner(datasetId, selectedMetric);

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
                    Row(
                      children: [
                        const Text('UTC', style: TextStyle(color: Colors.white60, fontSize: 12)),
                        Switch(
                          value: useUtc,
                          onChanged: (val) => appState.setUseUtc(val),
                        ),
                      ],
                    ),
                    const SizedBox(width: 8),
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
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      children: [
                        IconButton(
                          onPressed: () {
                            _refreshAll(datasetId, selectedMetric);
                          },
                          icon: const Icon(Icons.refresh),
                          tooltip: 'Refresh all widgets',
                        ),
                        if (_lastUpdated != null)
                          Text(
                            'Updated ${_lastUpdated!.hour}:${_lastUpdated!.minute.toString().padLeft(2, '0')}:${_lastUpdated!.second.toString().padLeft(2, '0')}',
                            style: const TextStyle(fontSize: 10, color: Colors.white30),
                          ),
                      ],
                    ),
                    OutlinedButton.icon(
                      onPressed: () {
                        final uri = Uri.base;
                        final ctxParams = appState.investigationContext?.toQueryParams() ?? {};
                        final link = uri.replace(queryParameters: {
                          ...uri.queryParameters,
                          'datasetId': datasetId,
                          'metric': selectedMetric,
                          ...ctxParams,
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
            if (freshnessBanner != null) ...[
              freshnessBanner,
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
    final useUtc = appState.useUtc;
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
          if (_buildFilterChips(appState).isNotEmpty) ...[
            const SizedBox(height: 12),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: _buildFilterChips(appState),
            ),
          ],
          const SizedBox(height: 16),
          FutureBuilder<DatasetSummary>(
            future: _summaryFuture,
            builder: (context, snapshot) {
              if (snapshot.connectionState == ConnectionState.waiting) {
                return const AnalyticsStatePanel(
                  state: AnalyticsState.loading,
                  title: 'Summary',
                  message: 'Loading dataset summary.',
                );
              }
              if (snapshot.hasError) {
                return AnalyticsStatePanel(
                  state: AnalyticsState.error,
                  title: 'Summary unavailable',
                  message: 'Request failed (timeout/auth). Retry.',
                  detail: snapshot.error.toString(),
                  onRetry: () {
                    setState(() => _load(datasetId, selectedMetric));
                  },
                );
              }
              final summary = snapshot.data;
              if (summary == null) {
                return const AnalyticsStatePanel(
                  state: AnalyticsState.empty,
                  title: 'No summary data',
                  message: 'No records in selected range.',
                );
              }
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
                child: FutureBuilder<TopKResponse>(
                  future: _topRegionsFuture,
                  builder: (context, snapshot) {
                    final meta = snapshot.data?.meta;
                    final truncated = meta?.truncated ?? false;
                    final total = meta?.totalDistinct;
                    final returned = meta?.returned ?? 0;
                    final limit = meta?.limit ?? 0;
                    
                    return ChartCard(
                      title: 'Top Regions',
                      pillLabel: limit > 0 ? 'Top $limit' : null,
                      truncated: truncated,
                      subtitle: total != null ? 'Showing $returned of $total' : null,
                      truncationLabel: 'Truncated',
                      truncationTooltip: 'This chart is showing the Top $limit values. Refine filters to see more.',
                      footerText: _asOfLabel(_keyTopRegions, useUtc: useUtc),
                      infoText: _buildCostInfo(meta),
                      child: Builder(builder: (context) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.loading,
                            title: 'Top regions',
                            message: 'Loading Top-K results.',
                          );
                        }
                        if (snapshot.hasError) {
                          return AnalyticsStatePanel(
                            state: AnalyticsState.error,
                            title: 'Top regions failed',
                            message: 'Request failed (timeout/auth). Retry.',
                            detail: snapshot.error.toString(),
                            onRetry: () {
                              setState(() => _load(datasetId, selectedMetric));
                            },
                          );
                        }
                        final items = snapshot.data?.items ?? [];
                        if (items.isEmpty) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.empty,
                            title: 'No regions',
                            message: 'No records in selected range.',
                          );
                        }
                        return BarChart(
                          values: items.map((e) => e.count.toDouble()).toList(),
                          labels: items.map((e) => e.label).toList(),
                          onTap: (i) {
                            if (items.length > i) {
                              final appState = context.read<AppState>();
                              appState.setFilterRegion(items[i].label);
                              _load(datasetId, selectedMetric);
                              _showRecordsBrowser(region: items[i].label);
                            }
                          },
                        );
                      }),
                    );
                  },
                ),
              ),
              SizedBox(
                width: 420,
                child: FutureBuilder<TopKResponse>(
                  future: _topAnomalyFuture,
                  builder: (context, snapshot) {
                    final meta = snapshot.data?.meta;
                    final truncated = meta?.truncated ?? false;
                    final total = meta?.totalDistinct;
                    final returned = meta?.returned ?? 0;
                    final limit = meta?.limit ?? 0;

                    return ChartCard(
                      title: 'Anomaly Types',
                      pillLabel: limit > 0 ? 'Top $limit' : null,
                      truncated: truncated,
                      subtitle: total != null ? 'Showing $returned of $total' : null,
                      truncationLabel: 'Truncated',
                      truncationTooltip: 'This chart is showing the Top $limit values. Refine filters to see more.',
                      footerText: _asOfLabel(_keyTopAnomaly, useUtc: useUtc),
                      infoText: _buildCostInfo(meta),
                      child: Builder(builder: (context) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.loading,
                            title: 'Anomaly types',
                            message: 'Loading Top-K results.',
                          );
                        }
                        if (snapshot.hasError) {
                          return AnalyticsStatePanel(
                            state: AnalyticsState.error,
                            title: 'Anomaly types failed',
                            message: 'Request failed (timeout/auth). Retry.',
                            detail: snapshot.error.toString(),
                            onRetry: () {
                              setState(() => _load(datasetId, selectedMetric));
                            },
                          );
                        }
                        final items = snapshot.data?.items ?? [];
                        if (items.isEmpty) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.empty,
                            title: 'No anomaly types',
                            message: 'No records in selected range.',
                          );
                        }
                        return BarChart(
                          values: items.map((e) => e.count.toDouble()).toList(),
                          labels: items.map((e) => e.label).toList(),
                          onTap: (i) {
                            if (items.length > i) {
                              final appState = context.read<AppState>();
                              appState.setFilterAnomalyType(items[i].label);
                              _load(datasetId, selectedMetric);
                              _showRecordsBrowser(anomalyType: items[i].label, isAnomaly: 'true');
                            }
                          },
                        );
                      }),
                    );
                  },
                ),
              ),
              SizedBox(
                width: 420,
                child: FutureBuilder<HistogramData>(
                  future: _metricHistFuture,
                  builder: (context, snapshot) {
                    final meta = snapshot.data?.meta;
                    final truncated = meta?.truncated ?? false;
                    final bins = meta?.limit ?? 0;
                    return ChartCard(
                      title: '$selectedMetric Histogram',
                      truncated: truncated,
                      subtitle: truncated && bins > 0 ? 'Bins capped at $bins' : null,
                      truncationLabel: 'Bins capped',
                      truncationTooltip: 'Requested bins exceeded the cap; histogram was downsampled.',
                      footerText: _asOfLabel(_keyMetricHist, useUtc: useUtc),
                      infoText: _buildCostInfo(meta),
                      child: Column(
                        children: [
                          Row(
                            mainAxisAlignment: MainAxisAlignment.end,
                            children: [
                              const Text('Anomalies', style: TextStyle(fontSize: 10, color: Colors.white54)),
                              SizedBox(
                                height: 24,
                                child: Switch(
                                  value: _showAnomalyOverlay, 
                                  onChanged: (v) { setState(() { _showAnomalyOverlay = v; _load(datasetId, selectedMetric); }); }
                                ),
                              )
                            ],
                          ),
                          Expanded(
                            child: Builder(builder: (context) {
                              if (snapshot.connectionState == ConnectionState.waiting) {
                                return const AnalyticsStatePanel(
                                  state: AnalyticsState.loading,
                                  title: 'Histogram',
                                  message: 'Loading distribution.',
                                );
                              }
                              if (snapshot.hasError) {
                                return AnalyticsStatePanel(
                                  state: AnalyticsState.error,
                                  title: 'Histogram failed',
                                  message: 'Request failed (timeout/auth). Retry.',
                                  detail: snapshot.error.toString(),
                                  onRetry: () {
                                    setState(() => _load(datasetId, selectedMetric));
                                  },
                                );
                              }
                              final hist = snapshot.data;
                              final values = hist?.counts.map((e) => e.toDouble()).toList() ?? [];
                              if (values.isEmpty) {
                                return const AnalyticsStatePanel(
                                  state: AnalyticsState.empty,
                                  title: 'No histogram data',
                                  message: 'No records in selected range.',
                                );
                              }
                              final labels = List.generate(values.length, (i) {
                                if (i < hist!.edges.length) {
                                  return hist.edges[i].toStringAsFixed(1);
                                }
                                return "";
                              });
                              
                              if (_showAnomalyOverlay && _metricHistAnomalyFuture != null) {
                                return FutureBuilder<HistogramData>(
                                    future: _metricHistAnomalyFuture,
                                    builder: (context, snapshotOverlay) {
                                      final overlayHist = snapshotOverlay.data;
                                      final overlayValues = overlayHist?.counts.map((e) => e.toDouble()).toList();
                                      return BarChart(values: values, labels: labels, overlayValues: overlayValues);
                                    }
                                );
                              }
                              return BarChart(values: values, labels: labels);
                            }),
                          ),
                        ],
                      ),
                    );
                  }
                ),
              ),
              SizedBox(
                width: 420,
                child: FutureBuilder<DatasetSummary>(
                  future: _summaryFuture,
                  builder: (context, snapshot) {
                    return ChartCard(
                      title: 'Anomaly Rate Trend ($_bucketLabel)',
                      pillLabels: [
                        '${_bucketLabel} buckets',
                        useUtc ? 'UTC' : 'Local',
                      ],
                      footerText: _asOfLabel(_keySummary, useUtc: useUtc),
                      infoText: _buildCostInfo(snapshot.data?.meta),
                      child: Builder(builder: (context) {
                        if (snapshot.connectionState == ConnectionState.waiting) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.loading,
                            title: 'Anomaly rate trend',
                            message: 'Loading trend data.',
                          );
                        }
                        if (snapshot.hasError) {
                          return AnalyticsStatePanel(
                            state: AnalyticsState.error,
                            title: 'Anomaly rate failed',
                            message: 'Request failed (timeout/auth). Retry.',
                            detail: snapshot.error.toString(),
                            onRetry: () {
                              setState(() => _load(datasetId, selectedMetric));
                            },
                          );
                        }
                        final trend = snapshot.data?.anomalyRateTrend ?? [];
                        if (trend.isEmpty) {
                          return const AnalyticsStatePanel(
                            state: AnalyticsState.empty,
                            title: 'No trend data',
                            message: 'No records in selected range.',
                          );
                        }
                        final xs = List<double>.generate(trend.length, (i) => i.toDouble());
                        final ys = trend.map((e) => e.anomalyRate).toList();
                        return LineChart(
                          x: xs,
                          y: ys,
                          xLabelBuilder: (val) {
                            int idx = val.round();
                            if (idx >= 0 && idx < trend.length) {
                              if (!_shouldShowTick(idx, trend.length)) return '';
                              try {
                                final dt = DateTime.parse(trend[idx].ts);
                                return formatBucketLabel(dt, useUtc: useUtc);
                              } catch (_) {}
                            }
                            return "";
                          },
                          yLabelBuilder: (val) => val.toStringAsFixed(2),
                        );
                      }),
                    );
                  },
                ),
              ),
            ],
          ),
          const SizedBox(height: 24),
          SizedBox(
            width: double.infinity,
            child: FutureBuilder<TimeSeriesResponse>(
              future: _metricTsFuture,
              builder: (context, snapshot) {
                return ChartCard(
                  title: '$selectedMetric Mean ($_bucketLabel)',
                  height: 240,
                  pillLabels: [
                    '${_bucketLabel} buckets',
                    useUtc ? 'UTC' : 'Local',
                  ],
                  footerText: _asOfLabel(_keyMetricTs, useUtc: useUtc),
                  infoText: _buildCostInfo(snapshot.data?.meta),
                  child: Builder(builder: (context) {
                    if (snapshot.connectionState == ConnectionState.waiting) {
                      return const AnalyticsStatePanel(
                        state: AnalyticsState.loading,
                        title: 'Time series',
                        message: 'Loading trend data.',
                      );
                    }
                    if (snapshot.hasError) {
                      return AnalyticsStatePanel(
                        state: AnalyticsState.error,
                        title: 'Time series failed',
                        message: 'Request failed (timeout/auth). Retry.',
                        detail: snapshot.error.toString(),
                        onRetry: () {
                          setState(() => _load(datasetId, selectedMetric));
                        },
                      );
                    }
                    final points = snapshot.data?.items ?? [];
                    if (points.isEmpty) {
                      return const AnalyticsStatePanel(
                        state: AnalyticsState.empty,
                        title: 'No trend data',
                        message: 'No records in selected range.',
                      );
                    }
                    final xs = List<double>.generate(points.length, (i) => i.toDouble());
                    final ys = points.map((e) => e.values['${selectedMetric}_mean'] ?? 0.0).toList();
                    
                    final maxCount = points.isEmpty ? 0 : points.map((e) => e.count).reduce((a, b) => a > b ? a : b);
                    final partial = points.map((e) => e.count < maxCount * 0.9).toList();
                    final hasPartial = partial.any((p) => p);

                    final chart = LineChart(
                      x: xs, 
                      y: ys,
                      partial: partial,
                      xLabelBuilder: (val) {
                        int idx = val.round();
                        if (idx >= 0 && idx < points.length) {
                           if (!_shouldShowTick(idx, points.length)) return '';
                           try {
                             final dt = DateTime.parse(points[idx].ts);
                             return formatBucketLabel(dt, useUtc: useUtc);
                           } catch (_) {}
                        }
                        return "";
                      },
                      yLabelBuilder: (val) => val.toStringAsFixed(1),
                      onTap: (i) {
                        if (points.length > i) {
                          final start = points[i].ts;
                          final bucketSeconds = snapshot.data?.bucketSeconds ?? 3600;
                          final next = bucketEndFromIso(start, bucketSeconds);
                          if (next != null) {
                            final appState = context.read<AppState>();
                            appState.setFilterBucket(start, next.toIso8601String());
                            _load(datasetId, selectedMetric);
                            _showRecordsBrowser(startTime: start, endTime: next.toIso8601String());
                          }
                        }
                      },
                    );
                    if (!hasPartial) return chart;
                    return Column(
                      children: [
                        const AnalyticsStatePanel(
                          state: AnalyticsState.partial,
                          title: 'Partial data',
                          message: 'Partial data: latest bucket incomplete.',
                        ),
                        const SizedBox(height: 8),
                        Expanded(child: chart),
                      ],
                    );
                  }),
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  List<Widget> _buildFilterChips(AppState appState) {
    final chips = <Widget>[];
    if (appState.filterRegion != null) {
      chips.add(_filterChip('region=${appState.filterRegion}', () {
        appState.setFilterRegion(null);
        _load(appState.datasetId!, appState.getSelectedMetric(appState.datasetId!));
      }));
    }
    if (appState.filterAnomalyType != null) {
      chips.add(_filterChip('type=${appState.filterAnomalyType}', () {
        appState.setFilterAnomalyType(null);
        _load(appState.datasetId!, appState.getSelectedMetric(appState.datasetId!));
      }));
    }
    if (appState.filterBucketStart != null || appState.filterBucketEnd != null) {
      chips.add(_filterChip('bucket', () {
        appState.setFilterBucket(null, null);
        _load(appState.datasetId!, appState.getSelectedMetric(appState.datasetId!));
      }));
    }
    if (chips.length > 1) {
      chips.add(TextButton(
        onPressed: () {
          appState.clearFilters();
          _load(appState.datasetId!, appState.getSelectedMetric(appState.datasetId!));
        },
        child: const Text('Clear all'),
      ));
    }
    return chips;
  }

  Widget _filterChip(String label, VoidCallback onClear) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: Colors.white10,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white24),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(label, style: const TextStyle(fontSize: 11, color: Colors.white70)),
          const SizedBox(width: 6),
          GestureDetector(
            onTap: onClear,
            child: const Icon(Icons.close, size: 12, color: Colors.white60),
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
                    _comparisonMetric, _comparisonHistFuture, true,
                    footerKey1: _keyMetricHist, footerKey2: _keyComparisonHist,
                    onRetry: () => _load(datasetId, selectedMetric),
                    useUtc: appState.useUtc),
                const SizedBox(height: 24),
                _buildChartRow('Trend ($_bucketLabel Mean)', selectedMetric, _metricTsFuture, _comparisonMetric,
                    _comparisonTsFuture, false,
                    footerKey1: _keyMetricTs, footerKey2: _keyComparisonTs,
                    onRetry: () => _load(datasetId, selectedMetric),
                    useUtc: appState.useUtc,
                    pillLabels1: [
                      '${_bucketLabel} buckets',
                      appState.useUtc ? 'UTC' : 'Local',
                    ],
                    pillLabels2: [
                      '${_bucketLabel} buckets',
                      appState.useUtc ? 'UTC' : 'Local',
                    ]),
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

  Widget _buildChartRow(String title, String m1, Future? f1, String? m2, Future? f2, bool isHist,
      {String? footerKey1,
      String? footerKey2,
      VoidCallback? onRetry,
      required bool useUtc,
      List<String>? pillLabels1,
      List<String>? pillLabels2}) {
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
                footerText: footerKey1 != null ? _asOfLabel(footerKey1, useUtc: useUtc) : null,
                pillLabels: pillLabels1,
                child: _buildChart(f1, isHist, m1, onRetry: onRetry, useUtc: useUtc),
              ),
            ),
            if (m2 != null) ...[
              const SizedBox(width: 16),
              Expanded(
                child: ChartCard(
                  title: m2,
                  footerText: footerKey2 != null ? _asOfLabel(footerKey2, useUtc: useUtc) : null,
                  pillLabels: pillLabels2,
                  child: _buildChart(f2, isHist, m2, color: const Color(0xFF818CF8), onRetry: onRetry, useUtc: useUtc),
                ),
              ),
            ],
          ],
        ),
      ],
    );
  }

  Widget _buildChart(Future? future, bool isHist, String metric,
      {Color? color, VoidCallback? onRetry, required bool useUtc}) {
    return FutureBuilder(
      future: future,
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const AnalyticsStatePanel(
            state: AnalyticsState.loading,
            title: 'Loading',
            message: 'Fetching chart data.',
          );
        }
        if (snapshot.hasError || snapshot.data == null) {
          return AnalyticsStatePanel(
            state: AnalyticsState.error,
            title: 'Chart failed',
            message: 'Request failed (timeout/auth). Retry.',
            detail: snapshot.error?.toString(),
            onRetry: onRetry,
          );
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
          if (values.isEmpty) {
            return const AnalyticsStatePanel(
              state: AnalyticsState.empty,
              title: 'No histogram data',
              message: 'No records in selected range.',
            );
          }
          return BarChart(values: values, labels: labels, barColor: color ?? const Color(0xFF818CF8));
        } else {
          final response = snapshot.data as TimeSeriesResponse;
          final points = response.items;
          if (points.isEmpty) {
            return const AnalyticsStatePanel(
              state: AnalyticsState.empty,
              title: 'No trend data',
              message: 'No records in selected range.',
            );
          }
          final xs = List<double>.generate(points.length, (i) => i.toDouble());
          final ys = points.map((e) => e.values['${metric}_mean'] ?? 0.0).toList();
          
          final maxCount = points.map((e) => e.count).reduce((a, b) => a > b ? a : b);
          final partial = points.map((e) => e.count < maxCount * 0.9).toList();
          final hasPartial = partial.any((p) => p);

          final chart = LineChart(
            x: xs, 
            y: ys, 
            lineColor: color ?? const Color(0xFF38BDF8),
            partial: partial,
            xLabelBuilder: (val) {
              int idx = val.round();
              if (idx >= 0 && idx < points.length) {
                 if (!_shouldShowTick(idx, points.length)) return '';
                 try {
                   final dt = DateTime.parse(points[idx].ts);
                   return formatBucketLabel(dt, useUtc: useUtc);
                 } catch (_) {}
              }
              return "";
            },
            yLabelBuilder: (val) => val.toStringAsFixed(1),
            onTap: (i) {
               if (points.length > i) {
                  final start = points[i].ts;
                  try {
                    final bucketSeconds = response.bucketSeconds ?? 3600;
                    final next = bucketEndFromIso(start, bucketSeconds);
                    if (next != null) {
                      final appState = context.read<AppState>();
                      appState.setFilterBucket(start, next.toIso8601String());
                      _load(appState.datasetId!, appState.getSelectedMetric(appState.datasetId!));
                      _showRecordsBrowser(startTime: start, endTime: next.toIso8601String());
                    }
                  } catch (_) {}
               }
            },
          );
          if (!hasPartial) return chart;
          return Column(
            children: [
              const AnalyticsStatePanel(
                state: AnalyticsState.partial,
                title: 'Partial data',
                message: 'Partial data: latest bucket incomplete.',
              ),
              const SizedBox(height: 8),
              Expanded(child: chart),
            ],
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
          return const AnalyticsStatePanel(
            state: AnalyticsState.loading,
            title: 'Metric stats',
            message: 'Loading summary statistics.',
          );
        }
        if (snapshot.hasError) {
          return AnalyticsStatePanel(
            state: AnalyticsState.error,
            title: 'Metric stats failed',
            message: 'Request failed (timeout/auth). Retry.',
            detail: snapshot.error.toString(),
            onRetry: () {
              setState(() => _load(datasetId, metric));
            },
          );
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

  void _showRecordsBrowser({String? region, String? anomalyType, String? isAnomaly, String? startTime, String? endTime}) {
     final appState = context.read<AppState>();
     final datasetId = appState.datasetId!;
     final metric = appState.getSelectedMetric(datasetId);
     final useUtc = appState.useUtc;
     final ctx = _buildContext(
       datasetId: datasetId,
       metric: metric,
       useUtc: useUtc,
       region: region,
       anomalyType: anomalyType,
       isAnomaly: isAnomaly,
       startTime: startTime,
       endTime: endTime,
       bucketStart: startTime,
       bucketEnd: endTime,
     );
     appState.setInvestigationContext(ctx);
     showModalBottomSheet(
        context: context,
        isScrollControlled: true,
        backgroundColor: const Color(0xFF0F172A),
        shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(20))),
        builder: (_) => RecordsBrowser(
           datasetId: datasetId,
           metric: metric,
           useUtc: useUtc,
           contextSeed: ctx,
           onContextChanged: appState.setInvestigationContext,
           region: region,
           anomalyType: anomalyType,
           isAnomaly: isAnomaly,
           startTime: startTime,
           endTime: endTime,
        )
     );
  }
}

class RecordsBrowser extends StatefulWidget {
  final String datasetId;
  final String metric;
  final bool useUtc;
  final InvestigationContext? contextSeed;
  final ValueChanged<InvestigationContext?>? onContextChanged;
  final String? region;
  final String? anomalyType;
  final String? isAnomaly;
  final String? startTime;
  final String? endTime;

  const RecordsBrowser({
    super.key,
    required this.datasetId,
    required this.metric,
    required this.useUtc,
    this.contextSeed,
    this.onContextChanged,
    this.region,
    this.anomalyType,
    this.isAnomaly,
    this.startTime,
    this.endTime,
  });

  @override
  State<RecordsBrowser> createState() => _RecordsBrowserState();
}

class _RecordsBrowserState extends State<RecordsBrowser> {
  final int _limit = 20;
  int _offset = 0;
  int _total = 0;
  List<Map<String, dynamic>> _items = [];
  bool _loading = false;
  String? _error;
  InvestigationContext? _context;

  @override
  void initState() {
    super.initState();
    if (widget.contextSeed != null) {
      _context = widget.contextSeed;
      _offset = widget.contextSeed!.offset ?? 0;
    }
    _load();
  }

  void _load() {
    setState(() {
      _loading = true;
      _error = null;
    });
    context.read<TelemetryService>().searchDatasetRecords(
      widget.datasetId,
      limit: _limit,
      offset: _offset,
      region: widget.region,
      anomalyType: widget.anomalyType,
      isAnomaly: widget.isAnomaly,
      startTime: widget.startTime,
      endTime: widget.endTime,
    ).then((data) {
      if (mounted) {
        setState(() {
          _items = (data['items'] as List).map((e) => Map<String, dynamic>.from(e)).toList();
          _total = data['total'] ?? 0;
          _loading = false;
          _context = _context?.copyWith(
                offset: _offset,
                limit: _limit,
                region: widget.region,
                anomalyType: widget.anomalyType,
                isAnomaly: widget.isAnomaly,
                startTime: widget.startTime,
                endTime: widget.endTime,
              ) ??
              InvestigationContext(
                datasetId: widget.datasetId,
                metric: widget.metric,
                useUtc: widget.useUtc,
                offset: _offset,
                limit: _limit,
                region: widget.region,
                anomalyType: widget.anomalyType,
                isAnomaly: widget.isAnomaly,
                startTime: widget.startTime,
                endTime: widget.endTime,
              );
          widget.onContextChanged?.call(_context);
        });
      }
    }).catchError((e) {
      if (mounted) {
        setState(() {
          _error = e.toString();
          _loading = false;
        });
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final chips = _contextChips();
    return Container(
      padding: const EdgeInsets.all(24),
      height: MediaQuery.of(context).size.height * 0.8,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('Records Browser', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                  Text(
                    '${widget.region ?? "All Regions"} • ${widget.anomalyType ?? "All Types"} • ${widget.startTime != null ? "Time Filtered" : "All Time"}',
                    style: const TextStyle(color: Colors.white54, fontSize: 12),
                  ),
                ],
              ),
              Row(
                children: [
                  TextButton.icon(
                    onPressed: () => Navigator.pop(context),
                    icon: const Icon(Icons.arrow_back, size: 16),
                    label: const Text('Back to aggregate'),
                  ),
                  IconButton(icon: const Icon(Icons.close), onPressed: () => Navigator.pop(context)),
                ],
              ),
            ],
          ),
          if (chips.isNotEmpty) ...[
            const SizedBox(height: 12),
            Wrap(spacing: 8, runSpacing: 8, children: chips),
          ],
          const SizedBox(height: 16),
          Expanded(
            child: _loading
                ? const AnalyticsStatePanel(
                    state: AnalyticsState.loading,
                    title: 'Records loading',
                    message: 'Fetching records for the selected filters.',
                  )
                : _error != null
                    ? AnalyticsStatePanel(
                        state: AnalyticsState.error,
                        title: 'Records failed',
                        message: 'Request failed (timeout/auth). Retry.',
                        detail: _error,
                        onRetry: _load,
                      )
                    : _items.isEmpty
                        ? const AnalyticsStatePanel(
                            state: AnalyticsState.empty,
                            title: 'No records found',
                            message: 'No records in selected range.',
                          )
                        : ListView.builder(
                            itemCount: _items.length,
                            itemBuilder: (context, index) {
                              final item = _items[index];
                              final isAnomaly = item['is_anomaly'] == true;
                              return Card(
                                color: isAnomaly ? Colors.red.withOpacity(0.1) : Colors.white.withOpacity(0.05),
                                margin: const EdgeInsets.only(bottom: 8),
                                child: ListTile(
                                  title: Text('${item['host_id']}'),
                                  subtitle: Text('${item['timestamp'].substring(11, 19)} • ${item['region']}'),
                                  trailing: Column(
                                    mainAxisAlignment: MainAxisAlignment.center,
                                    crossAxisAlignment: CrossAxisAlignment.end,
                                    children: [
                                      Text('CPU: ${item['cpu_usage'].toStringAsFixed(1)}%'),
                                      if (isAnomaly)
                                        Text(item['anomaly_type'] ?? 'ANOMALY',
                                            style: const TextStyle(color: Colors.redAccent, fontSize: 10)),
                                    ],
                                  ),
                                ),
                              );
                            },
                          ),
          ),
          _buildPagination(),
        ],
      ),
    );
  }

  List<Widget> _contextChips() {
    final items = <Widget>[];
    final tz = widget.useUtc ? 'UTC' : 'Local';
    items.add(_contextChip('TZ: $tz'));
    if (widget.region != null) items.add(_contextChip('Region: ${widget.region}'));
    if (widget.anomalyType != null) items.add(_contextChip('Type: ${widget.anomalyType}'));
    if (widget.isAnomaly != null) items.add(_contextChip('Anomaly: ${widget.isAnomaly}'));
    if (widget.startTime != null || widget.endTime != null) {
      items.add(_contextChip('Range: ${widget.startTime ?? "-"} → ${widget.endTime ?? "-"}'));
    }
    return items;
  }

  Widget _contextChip(String label) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: Colors.white10,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white24),
      ),
      child: Text(label, style: const TextStyle(fontSize: 11, color: Colors.white70)),
    );
  }

  Widget _buildPagination() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        IconButton(
          icon: const Icon(Icons.chevron_left),
          onPressed: _offset > 0 ? () {
            setState(() {
              _offset = (_offset - _limit).clamp(0, _total);
              _load();
            });
          } : null,
        ),
        Text('${_offset + 1}-${(_offset + _items.length).clamp(0, _total)} of $_total'),
        IconButton(
          icon: const Icon(Icons.chevron_right),
          onPressed: (_offset + _limit) < _total ? () {
            setState(() {
              _offset += _limit;
              _load();
            });
          } : null,
        ),
      ],
    );
  }
}
