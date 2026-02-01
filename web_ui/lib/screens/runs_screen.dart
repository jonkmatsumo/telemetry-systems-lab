import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';

class RunsScreen extends StatefulWidget {
  const RunsScreen({super.key});

  @override
  State<RunsScreen> createState() => _RunsScreenState();
}

class _RunsScreenState extends State<RunsScreen> {
  late Future<List<DatasetRun>> _runsFuture;
  Map<String, dynamic>? _selectedDetail;
  bool _loadingDetail = false;
  List<Map<String, String>> _schema = [];
  String? _selectedMetric;
  Map<String, dynamic>? _metricStats;
  bool _loadingStats = false;

  @override
  void initState() {
    super.initState();
    _runsFuture = context.read<TelemetryService>().listDatasets();
    _fetchSchema();
  }

  Future<void> _fetchSchema() async {
    try {
      final schema = await context.read<TelemetryService>().getMetricsSchema();
      if (mounted) setState(() => _schema = schema);
    } catch (e) {
      debugPrint('Failed to fetch schema: $e');
    }
  }

  Future<void> _fetchStats(String runId, String metric) async {
    setState(() {
      _selectedMetric = metric;
      _loadingStats = true;
      _metricStats = null;
    });
    try {
      final stats = await context.read<TelemetryService>().getMetricStats(runId, metric);
      if (mounted) setState(() => _metricStats = stats);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('Failed to load stats for $metric: $e'),
          backgroundColor: Colors.red,
        ));
      }
    } finally {
      if (mounted) setState(() => _loadingStats = false);
    }
  }

  Future<void> _refresh() async {
    setState(() {
      _runsFuture = context.read<TelemetryService>().listDatasets();
    });
    await _fetchSchema();
  }

  Future<void> _selectRun(DatasetRun run) async {
    setState(() {
      _loadingDetail = true;
      _selectedMetric = null;
      _metricStats = null;
    });
    try {
      final detail = await context.read<TelemetryService>().getDatasetDetail(run.runId);
      setState(() => _selectedDetail = detail);
      context.read<AppState>().setDataset(run.runId);
    } finally {
      setState(() => _loadingDetail = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text('Dataset Runs', style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
              IconButton(onPressed: _refresh, icon: const Icon(Icons.refresh)),
            ],
          ),
          const SizedBox(height: 16),
          Expanded(
            child: Row(
              children: [
                Expanded(
                  flex: 2,
                  child: FutureBuilder<List<DatasetRun>>(
                    future: _runsFuture,
                    builder: (context, snapshot) {
                      if (snapshot.connectionState == ConnectionState.waiting) {
                        return const Center(child: CircularProgressIndicator());
                      }
                      if (snapshot.hasError) {
                        return Center(child: Text('Error: ${snapshot.error}'));
                      }
                      final runs = snapshot.data ?? [];
                      if (runs.isEmpty) {
                        return const Center(child: Text('No runs yet.'));
                      }
                      return ListView.separated(
                        itemCount: runs.length,
                        separatorBuilder: (_, __) => const Divider(height: 1, color: Colors.white12),
                        itemBuilder: (context, index) {
                          final run = runs[index];
                          return ListTile(
                            title: Text(run.runId, style: const TextStyle(fontSize: 12)),
                            subtitle: Text('${run.status} • rows=${run.insertedRows}'),
                            trailing: Text(run.createdAt, style: const TextStyle(fontSize: 11, color: Colors.white54)),
                            onTap: () => _selectRun(run),
                          );
                        },
                      );
                    },
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  flex: 3,
                  child: Container(
                    decoration: BoxDecoration(
                      color: Colors.black.withOpacity(0.2),
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(color: Colors.white12),
                    ),
                    child: _loadingDetail
                        ? const Center(child: CircularProgressIndicator())
                        : _selectedDetail == null
                            ? const Center(child: Text('Select a run to view details'))
                            : DefaultTabController(
                                length: 3,
                                child: Column(
                                  children: [
                                    const TabBar(
                                      tabs: [
                                        Tab(text: 'Overview'),
                                        Tab(text: 'Features'),
                                        Tab(text: 'Models'),
                                      ],
                                    ),
                                    Expanded(
                                      child: TabBarView(
                                        children: [
                                          _buildOverview(_selectedDetail!),
                                          _buildFeatures(_selectedDetail!),
                                          _buildModelsTab(_selectedDetail!),
                                        ],
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildOverview(Map<String, dynamic> detail) {
    final runId = detail['run_id'] ?? '';
    return Padding(
      padding: const EdgeInsets.all(16),
      child: ListView(
        children: [
          _kv('Run ID', runId),
          _kv('Status', detail['status'] ?? ''),
          _kv('Inserted Rows', '${detail['inserted_rows'] ?? 0}'),
          _kv('Tier', detail['tier'] ?? ''),
          _kv('Host Count', '${detail['host_count'] ?? 0}'),
          _kv('Interval (s)', '${detail['interval_seconds'] ?? 0}'),
          _kv('Start Time', detail['start_time'] ?? ''),
          _kv('End Time', detail['end_time'] ?? ''),
          _kv('Created', detail['created_at'] ?? ''),
          if ((detail['error'] ?? '').toString().isNotEmpty) _kv('Error', detail['error']),
          const SizedBox(height: 24),
          ElevatedButton.icon(
            onPressed: () {
              final appState = context.read<AppState>();
              appState.setDataset(runId);
              appState.setTabIndex(0); // Go to Control
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF38BDF8),
              foregroundColor: const Color(0xFF0F172A),
              padding: const EdgeInsets.symmetric(vertical: 16),
            ),
            icon: const Icon(Icons.psychology),
            label: const Text('Train Model on this Dataset',
                style: TextStyle(fontWeight: FontWeight.bold)),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: () {
              final appState = context.read<AppState>();
              appState.setDataset(runId);
              appState.setTabIndex(2); // Go to Analytics
            },
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 16),
            ),
            icon: const Icon(Icons.analytics),
            label: const Text('View Analytics'),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: () {
              final uri = Uri.base;
              final link = uri.replace(queryParameters: {
                ...uri.queryParameters,
                'datasetId': runId,
              }).toString();
              Clipboard.setData(ClipboardData(text: link));
              ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Link copied to clipboard')));
            },
            icon: const Icon(Icons.link),
            label: const Text('Copy Shareable Link'),
          ),
        ],
      ),
    );
  }

  Widget _buildFeatures(Map<String, dynamic> detail) {
    if (_schema.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    return Column(
      children: [
        if (_selectedMetric != null) _buildStatsPanel(),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: ListView.separated(
              itemCount: _schema.length,
              separatorBuilder: (_, __) => const Divider(color: Colors.white12),
              itemBuilder: (context, index) {
                final f = _schema[index];
                final key = f['key'] ?? '';
                final isSelected = _selectedMetric == key;
                return ListTile(
                  title: Text(f['label'] ?? key,
                      style: TextStyle(
                          color: isSelected ? const Color(0xFF38BDF8) : Colors.white)),
                  subtitle: Text('${f['description'] ?? ''}\nType: ${f['type']} • Unit: ${f['unit']}'),
                  isThreeLine: true,
                  onTap: () => _fetchStats(detail['run_id'], key),
                  selected: isSelected,
                  selectedTileColor: Colors.white.withOpacity(0.05),
                );
              },
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildModelsTab(Map<String, dynamic> detail) {
    final runId = detail['run_id'];
    return FutureBuilder<List<Map<String, dynamic>>>(
      future: context.read<TelemetryService>().getDatasetModels(runId),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Center(child: CircularProgressIndicator());
        }
        if (snapshot.hasError) {
          return Center(child: Text('Error: ${snapshot.error}'));
        }
        final models = snapshot.data ?? [];
        if (models.isEmpty) {
          return const Center(child: Text('No models trained on this dataset yet.'));
        }
        return ListView.separated(
          itemCount: models.length,
          padding: const EdgeInsets.all(16),
          separatorBuilder: (_, __) => const Divider(color: Colors.white12),
          itemBuilder: (context, index) {
            final m = models[index];
            return ListTile(
              title: Text(m['name']),
              subtitle: Text('Status: ${m['status']} • Created: ${m['created_at']}'),
              trailing: ElevatedButton(
                onPressed: () {
                  final appState = context.read<AppState>();
                  appState.setModel(m['model_run_id']);
                  appState.setTabIndex(3); // Go to Models
                },
                child: const Text('View Model'),
              ),
            );
          },
        );
      },
    );
  }

  Widget _buildStatsPanel() {
    if (_loadingStats) return const LinearProgressIndicator();
    if (_metricStats == null) return const SizedBox.shrink();
    return Container(
      padding: const EdgeInsets.all(16),
      margin: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF38BDF8).withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF38BDF8).withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('Stats for $_selectedMetric',
                  style: const TextStyle(fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
              IconButton(
                icon: const Icon(Icons.close, size: 16),
                onPressed: () => setState(() => _selectedMetric = null),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 16,
            runSpacing: 8,
            children: [
              _statItem('Count', _metricStats!['count'].toString()),
              _statItem('Mean', (_metricStats!['mean'] as double).toStringAsFixed(3)),
              _statItem('Min', (_metricStats!['min'] as double).toStringAsFixed(3)),
              _statItem('Max', (_metricStats!['max'] as double).toStringAsFixed(3)),
              _statItem('p50', (_metricStats!['p50'] as double).toStringAsFixed(3)),
              _statItem('p95', (_metricStats!['p95'] as double).toStringAsFixed(3)),
            ],
          ),
        ],
      ),
    );
  }

  Widget _statItem(String label, String value) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: const TextStyle(color: Colors.white54, fontSize: 11)),
        Text(value, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
      ],
    );
  }

  Widget _kv(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          SizedBox(width: 140, child: Text(label, style: const TextStyle(color: Colors.white54))),
          Expanded(child: Text(value)),
        ],
      ),
    );
  }
}
