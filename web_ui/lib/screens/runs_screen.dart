import 'package:flutter/material.dart';
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

  @override
  void initState() {
    super.initState();
    _runsFuture = context.read<TelemetryService>().listDatasets();
  }

  Future<void> _refresh() async {
    setState(() {
      _runsFuture = context.read<TelemetryService>().listDatasets();
    });
  }

  Future<void> _selectRun(DatasetRun run) async {
    setState(() => _loadingDetail = true);
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
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: Colors.black.withOpacity(0.2),
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(color: Colors.white12),
                    ),
                    child: _loadingDetail
                        ? const Center(child: CircularProgressIndicator())
                        : _selectedDetail == null
                            ? const Center(child: Text('Select a run to view details'))
                            : _buildDetail(_selectedDetail!),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDetail(Map<String, dynamic> detail) {
    final runId = detail['run_id'] ?? '';
    return ListView(
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
