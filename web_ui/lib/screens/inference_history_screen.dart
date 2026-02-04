import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';

class InferenceHistoryScreen extends StatefulWidget {
  const InferenceHistoryScreen({super.key});

  @override
  State<InferenceHistoryScreen> createState() => _InferenceHistoryScreenState();
}

class _InferenceHistoryScreenState extends State<InferenceHistoryScreen> {
  Future<List<InferenceRunSummary>>? _runsFuture;
  Map<String, dynamic>? _detail;

  void _load(String? datasetId, String? modelRunId) {
    _runsFuture = context
        .read<TelemetryService>()
        .listInferenceRuns(datasetId: datasetId, modelRunId: modelRunId);
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final datasetId = appState.datasetId;
    final modelRunId = appState.modelRunId;
    if (_runsFuture == null) {
      _load(datasetId, modelRunId);
    }

    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text('Inference History', style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
              IconButton(
                onPressed: () {
                  setState(() => _load(datasetId, modelRunId));
                },
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Expanded(
            child: Row(
              children: [
                Expanded(
                  flex: 2,
                  child: FutureBuilder<List<InferenceRunSummary>>(
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
                        return const Center(child: Text('No inference runs found.'));
                      }
                      return ListView.separated(
                        itemCount: runs.length,
                        separatorBuilder: (context, _) => const Divider(height: 1, color: Colors.white12),
                        itemBuilder: (context, index) {
                          final run = runs[index];
                          return ListTile(
                            title: Text(run.inferenceId, style: const TextStyle(fontSize: 12)),
                            subtitle: Text('${run.status} • anomalies=${run.anomalyCount}'),
                            trailing: Text('${run.latencyMs.toStringAsFixed(1)} ms',
                                style: const TextStyle(fontSize: 11, color: Colors.white54)),
                            onTap: () async {
                              final detail =
                                  await context.read<TelemetryService>().getInferenceRun(run.inferenceId);
                              setState(() => _detail = detail);
                            },
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
                      color: Colors.black.withValues(alpha: 0.2),
                      borderRadius: BorderRadius.circular(12),
                      border: Border.all(color: Colors.white12),
                    ),
                    child: _detail == null ? const Center(child: Text('Select a run to view details')) : _buildDetail(),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDetail() {
    final detail = _detail!;
    return ListView(
      children: [
        _kv('Inference ID', detail['inference_id'] ?? ''),
        _kv('Model Run ID', detail['model_run_id'] ?? ''),
        _kv('Status', detail['status'] ?? ''),
        _kv('Anomaly Count', '${detail['anomaly_count'] ?? 0}'),
        _kv('Latency (ms)', '${detail['latency_ms'] ?? 0}'),
        const SizedBox(height: 12),
        const Text('Details JSON', style: TextStyle(fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        Text((detail['details'] ?? []).toString(), style: const TextStyle(fontSize: 12)),
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
