import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/charts.dart';
import '../widgets/copy_share_link_button.dart';
import '../widgets/inline_alert.dart';
import 'scoring_results_screen.dart';
import 'compare_models_screen.dart';

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
  double? _currentThreshold;

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

  Future<void> _selectById(String id) async {
    setState(() => _loadingDetail = true);
    try {
      final detail = await context.read<TelemetryService>().getModelDetail(id);
      if (!mounted) return;
      setState(() {
        _selectedDetail = detail;
        _evalMetrics = null;
        _errorDist = [];
        _jobStatus = null;
      });
      context.read<AppState>().setModel(id);
    } finally {
      if (mounted) {
        setState(() => _loadingDetail = false);
      }
    }
  }

  Future<void> _selectModel(ModelRunSummary model) async {
    _selectById(model.modelRunId);
  }

  Widget _badge(String label, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(color: color.withValues(alpha: 0.2), borderRadius: BorderRadius.circular(4), border: Border.all(color: color.withValues(alpha: 0.5))),
      child: Text(label, style: TextStyle(color: color, fontSize: 9, fontWeight: FontWeight.bold)),
    );
  }

  Widget _buildTrialsTable(List<dynamic> trials, String? bestTrialId) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.black26,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.white10),
      ),
      child: Table(
        columnWidths: const {
          0: FixedColumnWidth(50),
          1: FlexColumnWidth(2),
          2: FlexColumnWidth(1),
          3: FixedColumnWidth(100),
        },
        children: [
          const TableRow(
            decoration: BoxDecoration(color: Colors.white10),
            children: [
              Padding(padding: EdgeInsets.all(8), child: Text('#', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12))),
              Padding(padding: EdgeInsets.all(8), child: Text('Params', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12))),
              Padding(padding: EdgeInsets.all(8), child: Text('Status', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12))),
              Padding(padding: EdgeInsets.all(8), child: Text('Actions', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12))),
            ],
          ),
          ...trials.map((t) {
            final isBest = t['model_run_id'] == bestTrialId;
            final trialId = t['model_run_id'];
            return TableRow(
              children: [
                Padding(padding: const EdgeInsets.all(8), child: Text(t['trial_index']?.toString() ?? 'N/A', style: const TextStyle(fontSize: 12))),
                Padding(
                  padding: const EdgeInsets.all(8), 
                  child: Row(
                    children: [
                      if (isBest) const Padding(padding: EdgeInsets.only(right: 4), child: Icon(Icons.star, color: Colors.amber, size: 12)),
                      Expanded(child: Text(t['trial_params']?.toString() ?? 'N/A', style: const TextStyle(fontSize: 11, fontFamily: 'monospace'), overflow: TextOverflow.ellipsis)),
                    ],
                  )
                ),
                Padding(padding: const EdgeInsets.all(8), child: Text(t['status'] ?? 'N/A', style: TextStyle(fontSize: 11, color: _getStatusColor(t['status'] ?? '')))),
                Padding(
                  padding: const EdgeInsets.all(4), 
                  child: Row(
                    children: [
                      TextButton(
                        onPressed: () => _selectById(trialId), 
                        child: const Text('View', style: TextStyle(fontSize: 11))
                      ),
                      TextButton(
                        onPressed: () {
                          Navigator.push(
                            context,
                            MaterialPageRoute(
                              builder: (_) => CompareModelsScreen(
                                leftRunId: _selectedDetail!['model_run_id'],
                                rightRunId: trialId,
                              ),
                            ),
                          );
                        }, 
                        child: const Text('Compare', style: TextStyle(fontSize: 11))
                      ),
                    ],
                  )
                ),
              ],
            );
          }),
        ],
      ),
    );
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
    final service = context.read<TelemetryService>();
    final eval = await service.getModelEval(modelRunId, datasetId);
    final dist = await service.getErrorDistribution(modelRunId, datasetId, groupBy: 'anomaly_type');
    if (!mounted) return;
    setState(() {
      _evalMetrics = eval;
      _errorDist = dist;
      // Initialize threshold from artifact if possible
      final artifact = _selectedDetail?['artifact'] ?? {};
      _currentThreshold = (artifact['threshold'] ?? artifact['thresholds']?['reconstruction_error'])?.toDouble();
    });
  }

  String _formatCreatedAt(String raw) {
    if (raw.isEmpty) return '';
    final parsed = DateTime.tryParse(raw);
    if (parsed == null) return raw;
    final month = parsed.month.toString().padLeft(2, '0');
    final day = parsed.day.toString().padLeft(2, '0');
    final hour = parsed.hour.toString().padLeft(2, '0');
    final minute = parsed.minute.toString().padLeft(2, '0');
    return '${parsed.year}-$month-$day $hour:$minute';
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
                    const Text('Training Runs', style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
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
                        separatorBuilder: (context, _) => const Divider(height: 1, color: Colors.white12),
                        itemBuilder: (context, index) {
                          final config = model.trainingConfig;
                          final components = config['n_components'] ?? 'N/A';
                          final percentile = config['percentile'] ?? 'N/A';
                          final isTuning = model.hpoSummary != null;
                          final isTrial = model.parentRunId != null;

                          return ListTile(
                            title: Row(
                              children: [
                                Expanded(child: Text(model.name, style: const TextStyle(fontWeight: FontWeight.bold))),
                                if (isTuning) _badge('TUNING', Colors.purple),
                                if (isTrial) _badge('TRIAL #${model.trialIndex}', Colors.blueGrey),
                                if (!isTuning && !isTrial) _badge('SINGLE', Colors.blue),
                              ],
                            ),
                            subtitle: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text('${model.status} • Dataset: ${model.datasetId.substring(0, 8)}...'),
                                if (isTuning)
                                  Text(
                                    'Trials: ${model.hpoSummary!['completed_count']} / ${model.hpoSummary!['trial_count']} complete' +
                                    (model.hpoSummary!['best_metric_value'] != null ? ' • Best: ${model.hpoSummary!['best_metric_value'].toStringAsFixed(4)}' : ''),
                                    style: const TextStyle(color: Color(0xFF38BDF8), fontSize: 12),
                                  )
                                else
                                  Text('PCA: $components comps • $percentile%'),
                              ],
                            ),
                            isThreeLine: true,
                            trailing: Text(_formatCreatedAt(model.createdAt), style: const TextStyle(fontSize: 11, color: Colors.white54)),
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
              decoration: BoxDecoration(
                color: Colors.black.withValues(alpha: 0.2),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.white12),
              ),
              child: _loadingDetail
                  ? const Center(child: CircularProgressIndicator())
                  : _selectedDetail == null
                      ? const Center(child: Text('Select a model to view details'))
                      : DefaultTabController(
                          length: 2,
                          child: Column(
                            children: [
                              const TabBar(
                                tabs: [
                                  Tab(text: 'Overview'),
                                  Tab(text: 'Scored Datasets'),
                                ],
                              ),
                              Expanded(
                                child: TabBarView(
                                  children: [
                                    _buildDetail(datasetId),
                                    _buildScoredDatasetsTab(_selectedDetail!),
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
    );
  }

  Widget _buildDetail(String? datasetId) {
    final detail = _selectedDetail!;
    final modelRunId = detail['model_run_id'] ?? '';
    final artifact = detail['artifact'] ?? {};
    final thresholds = artifact['thresholds'] ?? {};
    final nComponents = artifact['model']?['n_components'] ?? 0;
    final thresholdValue = detail['threshold'] ?? thresholds['reconstruction_error'];
    final nComponentsValue = detail['n_components'] ?? nComponents;
    final artifactPath = (detail['artifact_path'] ?? '').toString();

    final hpoConfig = detail['hpo_config'];
    final isParent = hpoConfig != null;
    final parentRunId = detail['parent_run_id'];
    final trialIndex = detail['trial_index'];
    final trialParams = detail['trial_params'];
    final bestTrialId = detail['best_trial_run_id'];

    return Padding(
      padding: const EdgeInsets.all(16),
      child: ListView(
        children: [
          _kv('Model Run ID', modelRunId),
          _kv('Dataset ID', detail['dataset_id'] ?? ''),
          _kv('Status', detail['status'] ?? ''),
          if (parentRunId != null) _kvWidget('Parent Run', TextButton(onPressed: () => _selectById(parentRunId), child: Text(parentRunId))),
          if (trialIndex != null) _kv('Trial Index', trialIndex.toString()),
          if (trialParams != null) _kv('Trial Params', trialParams.toString()),
          
          const SizedBox(height: 12),
          const Text('Configuration', style: TextStyle(fontWeight: FontWeight.bold)),
          if (!isParent) ...[
            _kvWidget(
              'Artifact Path',
              SelectableText(
                artifactPath.isNotEmpty ? artifactPath : 'N/A',
                style: const TextStyle(fontFamily: 'monospace'),
              ),
            ),
            _kv('n_components', _displayValue(nComponentsValue, zeroIsNa: true)),
            _kv('Percentile (%)', _displayValue(detail['training_config']?['percentile'])),
            _kv('Feature Set', _displayValue(detail['training_config']?['feature_set'])),
            _kv('Anomaly Threshold', _displayValue(thresholdValue)),
          ],
          if (isParent) ...[
            _kv('Algorithm', hpoConfig['algorithm'] ?? 'N/A'),
            _kv('Max Trials', _displayValue(hpoConfig['max_trials'])),
            _kv('Search Space', hpoConfig['search_space']?.toString() ?? 'N/A'),
            if (bestTrialId != null) 
              _kvWidget('Best Trial', 
                Row(
                  children: [
                    const Icon(Icons.star, color: Colors.amber, size: 16),
                    const SizedBox(width: 4),
                    TextButton(onPressed: () => _selectById(bestTrialId), child: Text('View Best Trial ($bestTrialId)')),
                  ],
                )
              ),
          ],
          
          if (isParent && detail['trials'] != null) ...[
            const SizedBox(height: 24),
            const Text('Trials', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16, color: Color(0xFF38BDF8))),
            const SizedBox(height: 8),
            _buildTrialsTable(detail['trials'], bestTrialId),
          ],

          if (detail['artifact_error'] != null && detail['artifact_error'].toString().isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: InlineAlert(title: 'Artifact Error', message: detail['artifact_error'].toString()),
            ),
          if (detail['error'] != null && detail['error'].toString().isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 16),
              child: InlineAlert(title: 'Training Error', message: detail['error'].toString()),
            ),
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
                OutlinedButton.icon(
                  onPressed: () {
                    Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => CompareModelsScreen(leftRunId: modelRunId),
                      ),
                    );
                  },
                  icon: const Icon(Icons.compare_arrows),
                  label: const Text('Compare'),
                ),
                ElevatedButton.icon(
                  onPressed: () {
                    final appState = context.read<AppState>();
                    appState.setModel(modelRunId);
                    appState.setTabIndex(0); // Go to Control
                  },
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF38BDF8),
                    foregroundColor: const Color(0xFF0F172A),
                  ),
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('Inference Preview'),
                ),
                const SizedBox(height: 12),
                CopyShareLinkButton(
                  label: 'Copy Shareable Link',
                  overrideParams: {
                    if ((detail['dataset_id'] ?? '').toString().isNotEmpty)
                      'datasetId': detail['dataset_id'].toString(),
                    if (modelRunId.isNotEmpty) 'modelId': modelRunId,
                  },
                ),
              ],
            ),
          if (_jobStatus != null)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Scoring job: ${_jobStatus!.status} (${_jobStatus!.processedRows}/${_jobStatus!.totalRows})',
                  ),
                  if (_jobStatus!.error.isNotEmpty)
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: InlineAlert(title: 'Scoring Error', message: _jobStatus!.error),
                    ),
                ],
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
          const Text('Evaluation Metrics', style: TextStyle(fontWeight: FontWeight.bold)),
          if (_evalMetrics == null)
            const Padding(
              padding: EdgeInsets.only(top: 8),
              child: Text(
                'Load evaluation metrics to view ROC/PR curves and confusion stats.',
                style: TextStyle(color: Colors.white60),
              ),
            ),
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
          _buildEvalSummary(),
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
      ),
    );
  }

  Widget _buildEvalSummary() {
    if (_evalMetrics == null || _currentThreshold == null) return const SizedBox.shrink();

    final detail = _selectedDetail!;
    final isTrial = detail['parent_run_id'] != null;
    final bestMetricValue = detail['best_metric_value'];
    final bestMetricName = detail['best_metric_name'];

    // Find closest point in PR curve
    Map<String, double>? closest;
    double minDist = double.infinity;
    for (final p in _evalMetrics!.pr) {
      final t = p['threshold'] ?? 0.0;
      final d = (t - _currentThreshold!).abs();
      if (d < minDist) {
        minDist = d;
        closest = p;
      }
    }

    if (closest == null) return const SizedBox.shrink();

    final p = closest['precision'] ?? 0.0;
    final r = closest['recall'] ?? 0.0;
    final f1 = (p + r) > 0 ? 2 * (p * r) / (p + r) : 0.0;

    // Threshold range
    double minT = double.infinity;
    double maxT = double.negativeInfinity;
    for (final pt in _evalMetrics!.pr) {
      final t = pt['threshold'] ?? 0.0;
      if (t < minT) minT = t;
      if (t > maxT) maxT = t;
    }
    if (minT == double.infinity || minT == maxT) {
      minT = 0;
      maxT = 1;
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SizedBox(height: 24),
        const Text('Threshold Analysis',
            style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
        const SizedBox(height: 16),
        Row(
          children: [
            Expanded(
              child: Slider(
                value: _currentThreshold!.clamp(minT, maxT),
                min: minT,
                max: maxT,
                onChanged: (val) => setState(() => _currentThreshold = val),
              ),
            ),
            Container(
              padding: const EdgeInsets.all(8),
              decoration: BoxDecoration(color: Colors.black26, borderRadius: BorderRadius.circular(4)),
              child: Text('Threshold: ${_currentThreshold!.toStringAsFixed(4)}'),
            ),
          ],
        ),
        const SizedBox(height: 16),
        Wrap(
          spacing: 16,
          runSpacing: 16,
          children: [
            _metricCard('Precision', p.toStringAsFixed(4)),
            _metricCard('Recall', r.toStringAsFixed(4)),
            _metricCard('F1 Score', f1.toStringAsFixed(4)),
            if (isTrial && bestMetricValue != null)
              _metricCard('Tuning Metric ($bestMetricName)', bestMetricValue.toStringAsFixed(6), color: Colors.purpleAccent),
          ],
        ),
      ],
    );
  }

  Widget _buildScoredDatasetsTab(Map<String, dynamic> detail) {
    final modelRunId = detail['model_run_id'];
    return FutureBuilder<List<Map<String, dynamic>>>(
      future: context.read<TelemetryService>().getModelScoredDatasets(modelRunId),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Center(child: CircularProgressIndicator());
        }
        if (snapshot.hasError) {
          return Center(child: Text('Error: ${snapshot.error}'));
        }
        final datasets = snapshot.data ?? [];
        if (datasets.isEmpty) {
          return const Center(child: Text('This model hasn\'t scored any datasets yet.'));
        }
        return ListView.separated(
          itemCount: datasets.length,
          padding: const EdgeInsets.all(16),
          separatorBuilder: (context, _) => const Divider(color: Colors.white12),
          itemBuilder: (context, index) {
            final ds = datasets[index];
            return ListTile(
              title: Text('Dataset: ${ds['dataset_id'].substring(0, 8)}...'),
              subtitle: Text('Scored at: ${ds['scored_at']}'),
              trailing: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  TextButton(
                    onPressed: () {
                      final appState = context.read<AppState>();
                      appState.setDataset(ds['dataset_id']);
                      appState.setTabIndex(1); // Go to Runs
                    },
                    child: const Text('View Dataset'),
                  ),
                  const SizedBox(width: 8),
                  ElevatedButton(
                    onPressed: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (_) => ScoringResultsScreen(
                            datasetId: ds['dataset_id'],
                            modelRunId: modelRunId,
                          ),
                        ),
                      );
                    },
                    child: const Text('Open Results'),
                  ),
                ],
              ),
            );
          },
        );
      },
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

  Widget _kvWidget(String label, Widget value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          SizedBox(width: 140, child: Text(label, style: const TextStyle(color: Colors.white54))),
          Expanded(child: value),
        ],
      ),
    );
  }

  String _displayValue(dynamic value, {bool zeroIsNa = false}) {
    if (value == null) return 'N/A';
    if (value is num) {
      if (zeroIsNa && value == 0) return 'N/A';
      return value.toString();
    }
    final text = value.toString();
    return text.isEmpty ? 'N/A' : text;
  }

  Widget _metricCard(String title, String value, {Color? color}) {
    return Container(
      width: 220,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.black.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color?.withValues(alpha: 0.5) ?? Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: TextStyle(color: color ?? Colors.white60, fontSize: 12)),
          const SizedBox(height: 6),
          Text(value, style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, color: color)),
        ],
      ),
    );
  }
}
