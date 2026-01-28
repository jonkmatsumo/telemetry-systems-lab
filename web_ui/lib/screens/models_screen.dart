import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/charts.dart';

class ModelsScreen extends StatefulWidget {
  const ModelsScreen({super.key});

  @override
  State<ModelsScreen> createState() => _ModelsScreenState();
}

class _ModelsScreenState extends State<ModelsScreen> {
  late Future<List<ModelRunSummary>> _modelsFuture;
  Map<String, dynamic>? _selectedDetail;
  EvalMetrics? _evalMetrics;
  List<ErrorDistributionEntry> _errorDist = [];
  bool _loadingDetail = false;
  ScoreJobStatus? _jobStatus;
  Timer? _jobTimer;
  bool _jobPollingInFlight = false;

  @override
  void initState() {
    super.initState();
    _modelsFuture = context.read<TelemetryService>().listModels();
  }

  @override
  void dispose() {
    _jobTimer?.cancel();
    super.dispose();
  }

  Future<void> _refresh() async {
    setState(() {
      _modelsFuture = context.read<TelemetryService>().listModels();
    });
  }

  Future<void> _selectModel(ModelRunSummary model) async {
    setState(() => _loadingDetail = true);
    try {
      final detail = await context.read<TelemetryService>().getModelDetail(model.modelRunId);
      setState(() => _selectedDetail = detail);
      context.read<AppState>().setModel(model.modelRunId);
    } finally {
      setState(() => _loadingDetail = false);
    }
  }

  Future<void> _startScoring(String datasetId, String modelRunId) async {
    final jobId = await context.read<TelemetryService>().startScoreJob(datasetId, modelRunId);
    _jobTimer?.cancel();
    _jobTimer = Timer.periodic(const Duration(seconds: 5), (timer) async {
      if (_jobPollingInFlight) return;
      _jobPollingInFlight = true;
      try {
        final status = await context.read<TelemetryService>().getJobProgress(jobId);
        setState(() => _jobStatus = status);
        if (status.status != 'RUNNING' && status.status != 'PENDING') {
          timer.cancel();
        }
      } finally {
        _jobPollingInFlight = false;
      }
    });
  }

  Future<void> _refreshJobStatus() async {
    if (_jobStatus == null) return;
    final status = await context.read<TelemetryService>().getJobProgress(_jobStatus!.jobId);
    setState(() => _jobStatus = status);
  }

  Future<void> _loadEval(String datasetId, String modelRunId) async {
    final eval = await context.read<TelemetryService>().getModelEval(modelRunId, datasetId);
    final dist = await context
        .read<TelemetryService>()
        .getErrorDistribution(modelRunId, datasetId, groupBy: 'anomaly_type');
    setState(() {
      _evalMetrics = eval;
      _errorDist = dist;
    });
  }

  @override
  Widget build(BuildContext context) {
    final datasetId = context.watch<AppState>().datasetId;
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Row(
        children: [
          Expanded(
            flex: 2,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Text('Model Runs', style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
                    IconButton(onPressed: _refresh, icon: const Icon(Icons.refresh)),
                  ],
                ),
                const SizedBox(height: 12),
                Expanded(
                  child: FutureBuilder<List<ModelRunSummary>>(
                    future: _modelsFuture,
                    builder: (context, snapshot) {
                      if (snapshot.connectionState == ConnectionState.waiting) {
                        return const Center(child: CircularProgressIndicator());
                      }
                      if (snapshot.hasError) {
                        return Center(child: Text('Error: ${snapshot.error}'));
                      }
                      final models = snapshot.data ?? [];
                      return ListView.separated(
                        itemCount: models.length,
                        separatorBuilder: (_, __) => const Divider(height: 1, color: Colors.white12),
                        itemBuilder: (context, index) {
                          final model = models[index];
                          return ListTile(
                            title: Text(model.name),
                            subtitle: Text('${model.status} • ${model.modelRunId}'),
                            trailing: Text(model.createdAt, style: const TextStyle(fontSize: 11, color: Colors.white54)),
                            onTap: () => _selectModel(model),
                          );
                        },
                      );
                    },
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 16),
          Expanded(
            flex: 4,
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
                      ? const Center(child: Text('Select a model to view details'))
                      : _buildDetail(datasetId),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDetail(String? datasetId) {
    final detail = _selectedDetail!;
    final modelRunId = detail['model_run_id'] ?? '';
    final artifact = detail['artifact'] ?? {};
    final thresholds = artifact['thresholds'] ?? {};
    final nComponents = artifact['model']?['n_components'] ?? 0;

    return ListView(
      children: [
        _kv('Model Run ID', modelRunId),
        _kv('Dataset ID', detail['dataset_id'] ?? ''),
        _kv('Status', detail['status'] ?? ''),
        _kv('Artifact Path', detail['artifact_path'] ?? ''),
        _kv('Threshold', '${thresholds['reconstruction_error'] ?? ''}'),
        _kv('Components', '$nComponents'),
        const SizedBox(height: 12),
        if (datasetId != null)
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              ElevatedButton(
                onPressed: () => _startScoring(datasetId, modelRunId),
                child: const Text('Score Dataset'),
              ),
              ElevatedButton(
                onPressed: () => _loadEval(datasetId, modelRunId),
                child: const Text('Load Eval Metrics'),
              ),
            ],
          ),
        if (_jobStatus != null)
          Padding(
            padding: const EdgeInsets.only(top: 12),
            child: Text(
              'Scoring job: ${_jobStatus!.status} (${_jobStatus!.processedRows}/${_jobStatus!.totalRows}) ${_jobStatus!.error}',
            ),
          ),
        if (_jobStatus != null)
          Padding(
            padding: const EdgeInsets.only(top: 8),
            child: OutlinedButton.icon(
              onPressed: _refreshJobStatus,
              icon: const Icon(Icons.refresh),
              label: const Text('Refresh Job Status'),
            ),
          ),
        const SizedBox(height: 16),
        if (_evalMetrics != null)
          Wrap(
            spacing: 16,
            runSpacing: 16,
            children: [
              _metricCard('Confusion',
                  'TP ${_evalMetrics!.confusion['tp']} | FP ${_evalMetrics!.confusion['fp']}\nTN ${_evalMetrics!.confusion['tn']} | FN ${_evalMetrics!.confusion['fn']}'),
              SizedBox(
                width: 360,
                child: ChartCard(
                  title: 'ROC Curve',
                  child: _buildLineFromPairs(_evalMetrics!.roc, 'fpr', 'tpr'),
                ),
              ),
              SizedBox(
                width: 360,
                child: ChartCard(
                  title: 'PR Curve',
                  child: _buildLineFromPairs(_evalMetrics!.pr, 'recall', 'precision'),
                ),
              ),
            ],
          ),
        if (_errorDist.isNotEmpty) const SizedBox(height: 16),
        if (_errorDist.isNotEmpty)
          SizedBox(
            width: 420,
            child: ChartCard(
              title: 'Error by Anomaly Type (mean)',
              child: BarChart(values: _errorDist.map((e) => e.mean).toList()),
            ),
          ),
      ],
    );
  }

  Widget _buildLineFromPairs(List<Map<String, double>> pairs, String xKey, String yKey) {
    if (pairs.isEmpty) return const SizedBox.shrink();
    final xs = <double>[];
    final ys = <double>[];
    for (final p in pairs) {
      xs.add(p[xKey] ?? 0.0);
      ys.add(p[yKey] ?? 0.0);
    }
    return LineChart(x: xs, y: ys);
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

  Widget _metricCard(String title, String value) {
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
          Text(title, style: const TextStyle(color: Colors.white60, fontSize: 12)),
          const SizedBox(height: 6),
          Text(value, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}
