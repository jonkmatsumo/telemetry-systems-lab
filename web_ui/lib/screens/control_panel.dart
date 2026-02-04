import 'dart:async';
import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/copy_share_link_button.dart';
import '../widgets/inline_alert.dart';

class ControlPanel extends StatefulWidget {
  const ControlPanel({super.key});

  @override
  State<ControlPanel> createState() => _ControlPanelState();
}

class _ControlPanelState extends State<ControlPanel> {
  final _hostCountController = TextEditingController(text: '5');
  final _modelNameController = TextEditingController(text: 'pca_v1');
  final _nComponentsController = TextEditingController(text: '3');
  final _percentileController = TextEditingController(text: '99.5');

  // HPO State
  bool _hpoEnabled = false;
  String _hpoAlgorithm = 'grid';
  final _hpoMaxTrialsController = TextEditingController(text: '10');
  final _hpoNComponentsSpaceController = TextEditingController(text: '2,3,4');
  final _hpoPercentileSpaceController = TextEditingController(text: '99.0,99.5,99.9');

  bool _loading = false;
  InferenceResponse? _inferenceResults;
  Timer? _pollingTimer;
  bool _pollingInFlight = false;

  List<Map<String, dynamic>> _datasetSamples = [];
  bool _loadingSamples = false;

  List<DatasetRun> _availableDatasets = [];
  List<ModelRunSummary> _availableModels = [];
  bool _fetchingResources = false;
  String? _selectionWarning;
  String? _pendingInferenceMessage;

  @override
  void initState() {
    super.initState();
    _fetchResources();
  }

  Future<void> _fetchResources() async {
    if (_fetchingResources) return;
    setState(() => _fetchingResources = true);
    final service = context.read<TelemetryService>();
    try {
      final datasets = await service.listDatasets(limit: 50, offset: 0);
      final models = await service.listModels(limit: 50, offset: 0);
      if (!mounted) return;
      final appState = context.read<AppState>();
      final datasetId = appState.datasetId;
      final modelId = appState.modelRunId;
      final datasetMissing =
          datasetId != null && !datasets.any((d) => d.runId == datasetId);
      final modelMissing =
          modelId != null && !models.any((m) => m.modelRunId == modelId);

      if (datasetMissing) {
        appState.setDataset(null);
        appState.setModel(null);
      } else if (modelMissing) {
        appState.setModel(null);
      }

      String? warning;
      if (datasetMissing && modelMissing) {
        warning =
            'Selected dataset and model are no longer available. Selections cleared.';
      } else if (datasetMissing) {
        warning = 'Selected dataset is no longer available. Selection cleared.';
      } else if (modelMissing) {
        warning = 'Selected model is no longer available. Selection cleared.';
      }

      setState(() {
        _availableDatasets = datasets;
        _availableModels = models;
        _selectionWarning = warning;
      });
    } catch (e) {
      if (!mounted) return;
      _showError('Failed to fetch resources: $e');
    } finally {
      if (mounted) {
        setState(() => _fetchingResources = false);
      }
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _syncWithAppState();
    _checkPendingInference();
  }
  
  void _checkPendingInference() {
    final appState = context.read<AppState>();
    final pending = appState.pendingInference;
    
    if (pending == null) return;

    WidgetsBinding.instance.addPostFrameCallback((_) async {
      if (!mounted) return;

      // Clear it immediately to prevent loops, but keep local ref
      appState.clearPendingInference();

      // Ensure we are in the right context (should be handled by nav, but double check)
      if (appState.datasetId != pending.datasetId || appState.modelRunId != pending.modelId) {
        // Mismatch context, just ignore or log?
        // For this task, we assume context is set by the caller (ScoringResultsScreen)
      }

      Map<String, dynamic>? payload = pending.recordPayload;

      // If payload missing, fetch it (though we expect it passed)
      if (payload == null) {
        try {
          final recordId = int.tryParse(pending.recordId);
          if (recordId == null) {
            _showError('Invalid record id: ${pending.recordId}');
            return;
          }
          final service = context.read<TelemetryService>();
          payload = await service.getDatasetRecord(pending.datasetId, recordId);
        } catch (e) {
          _showError('Failed to fetch pending record: $e');
          return;
        }
      }

      setState(() {
        _pendingInferenceMessage = 'Loaded record ${pending.recordId} from results.';
      });

      // Run inference
      _inferWithSample(payload);
    });
  }

  Future<void> _fetchSamples() async {
    final appState = context.read<AppState>();
    if (appState.datasetId == null) return;
    setState(() => _loadingSamples = true);
    final service = context.read<TelemetryService>();
    try {
      final samples = await service.getDatasetSamples(appState.datasetId!);
      setState(() => _datasetSamples = samples);
    } catch (e) {
      _showError('Failed to fetch samples: $e');
    } finally {
      setState(() => _loadingSamples = false);
    }
  }

  void _syncWithAppState() async {
    final appState = context.read<AppState>();
    final service = context.read<TelemetryService>();

    if (appState.datasetId != null && appState.currentDataset == null) {
      try {
        final status = await service.getDatasetStatus(appState.datasetId!);
        appState.setDataset(status.runId, status: status);
        _fetchSamples();
      } catch (e) {
        debugPrint('Sync dataset failed: $e');
      }
    } else if (appState.datasetId != null && _datasetSamples.isEmpty && !_loadingSamples) {
      _fetchSamples();
    }
    if (appState.modelRunId != null && appState.currentModel == null) {
      try {
        final status = await service.getModelStatus(appState.modelRunId!);
        appState.setModel(status.modelRunId, status: status);
      } catch (e) {
        debugPrint('Sync model failed: $e');
      }
    }
  }

  @override
  void dispose() {
    _pollingTimer?.cancel();
    _hostCountController.dispose();
    _modelNameController.dispose();
    _nComponentsController.dispose();
    _percentileController.dispose();
    _hpoMaxTrialsController.dispose();
    _hpoNComponentsSpaceController.dispose();
    _hpoPercentileSpaceController.dispose();
    super.dispose();
  }

  void _startPolling(String id, String type) {
    _pollingTimer?.cancel();
    _pollingTimer = Timer.periodic(const Duration(seconds: 4), (timer) async {
      if (_pollingInFlight) return;
      _pollingInFlight = true;
      final service = context.read<TelemetryService>();
      final appState = context.read<AppState>();
      try {
        if (type == 'dataset') {
          final status = await service.getDatasetStatus(id);
          appState.setDataset(status.runId, status: status);
          if (status.status != 'PENDING' && status.status != 'RUNNING') {
            timer.cancel();
            setState(() => _loading = false);
          }
        } else {
          final status = await service.getModelStatus(id);
          appState.setModel(status.modelRunId, status: status);
          if (status.status != 'PENDING' && status.status != 'RUNNING') {
            timer.cancel();
            setState(() => _loading = false);
          }
        }
      } catch (e) {
        timer.cancel();
        setState(() => _loading = false);
        _showError(e.toString());
      } finally {
        _pollingInFlight = false;
      }
    });
  }

  Future<void> _refreshStatus() async {
    final service = context.read<TelemetryService>();
    final appState = context.read<AppState>();
    _fetchResources();
    try {
      if (appState.datasetId != null) {
        final status = await service.getDatasetStatus(appState.datasetId!);
        appState.setDataset(status.runId, status: status);
      }
      if (appState.modelRunId != null) {
        final status = await service.getModelStatus(appState.modelRunId!);
        appState.setModel(status.modelRunId, status: status);
      }
    } catch (e) {
      _showError(e.toString());
    }
  }

  void _generate() async {
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final count = int.parse(_hostCountController.text);
      final runId = await service.generateDataset(count);
      if (!mounted) return;
      context.read<AppState>().setDataset(runId);
      _startPolling(runId, 'dataset');
      _fetchResources(); // Refresh list after starting generation
    } catch (e) {
      if (!mounted) return;
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _train() async {
    final appState = context.read<AppState>();
    if (appState.datasetId == null) return;
    
    final nComponents = int.tryParse(_nComponentsController.text);
    final percentile = double.tryParse(_percentileController.text);

    if (nComponents == null || nComponents <= 0 || nComponents > 5) {
      _showError('N Components must be between 1 and 5');
      return;
    }
    if (percentile == null || percentile < 50.0 || percentile >= 100.0) {
      _showError('Percentile must be between 50.0 and 99.99');
      return;
    }

    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      Map<String, dynamic>? hpoConfig;
      if (_hpoEnabled) {
        final nSpace = _hpoNComponentsSpaceController.text.split(',').map((e) => int.tryParse(e.trim())).whereType<int>().toList();
        final pSpace = _hpoPercentileSpaceController.text.split(',').map((e) => double.tryParse(e.trim())).whereType<double>().toList();
        final maxTrials = int.tryParse(_hpoMaxTrialsController.text) ?? 10;

        if (nSpace.isEmpty && pSpace.isEmpty) {
          _showError('HPO Search Space cannot be empty');
          setState(() => _loading = false);
          return;
        }

        hpoConfig = {
          'algorithm': _hpoAlgorithm,
          'max_trials': maxTrials,
          'search_space': {
            'n_components': nSpace,
            'percentile': pSpace,
          }
        };
      }

      final modelId = await service.trainModel(
        appState.datasetId!,
        name: _modelNameController.text,
        nComponents: nComponents,
        percentile: percentile,
        hpoConfig: hpoConfig,
      );
      appState.setModel(modelId);
      _startPolling(modelId, 'model');
      _fetchResources(); // Refresh list after starting training
    } catch (e) {
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _infer() async {
    final appState = context.read<AppState>();
    if (appState.modelRunId == null) return;
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final samples = [
        {
          'cpu_usage': 98.0,
          'memory_usage': 95.0,
          'disk_utilization': 30.0,
          'network_rx_rate': 10.0,
          'network_tx_rate': 5.0
        },
        {
          'cpu_usage': 40.0,
          'memory_usage': 50.0,
          'disk_utilization': 30.0,
          'network_rx_rate': 10.0,
          'network_tx_rate': 5.0
        }
      ];
      final res = await service.runInference(appState.modelRunId!, samples);
      setState(() {
        _inferenceResults = res;
        _loading = false;
      });
    } catch (e) {
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _inferWithSample(Map<String, dynamic> sample) async {
    final appState = context.read<AppState>();
    if (appState.modelRunId == null) return;
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final res = await service.runInference(appState.modelRunId!, [sample]);
      setState(() {
        _inferenceResults = res;
        _loading = false;
      });
    } catch (e) {
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _showError(String message) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), backgroundColor: Colors.red),
    );
  }

  Widget _buildDatasetSelector(AppState appState) {
    if (_availableDatasets.isEmpty && !_fetchingResources) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 8),
        child: Text('No datasets available. Generate a dataset to get started.',
            style: TextStyle(color: Colors.amberAccent, fontSize: 13)),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text('Or Select Existing Dataset',
            style: TextStyle(color: Color(0xFF94A3B8), fontSize: 14)),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: BoxDecoration(
            color: const Color(0xFF020617),
            borderRadius: BorderRadius.circular(8),
          ),
          child: DropdownButtonHideUnderline(
            child: DropdownButton<String>(
              isExpanded: true,
              value: _availableDatasets.any((d) => d.runId == appState.datasetId)
                  ? appState.datasetId
                  : null,
              hint: const Text('Choose a dataset'),
              dropdownColor: const Color(0xFF020617),
              items: _availableDatasets.map((d) {
                return DropdownMenuItem(
                  value: d.runId,
                  child: Text(_formatDatasetLabel(d), style: const TextStyle(fontSize: 14)),
                );
              }).toList(),
              onChanged: (val) {
                if (val != null) {
                  appState.setDataset(val);
                  _syncWithAppState();
                }
              },
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildModelSelector(AppState appState) {
    if (_availableModels.isEmpty && !_fetchingResources) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 8),
        child: Text('No models available. Train a model to enable inference.',
            style: TextStyle(color: Colors.amberAccent, fontSize: 13)),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text('Or Select Existing Model',
            style: TextStyle(color: Color(0xFF94A3B8), fontSize: 14)),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: BoxDecoration(
            color: const Color(0xFF020617),
            borderRadius: BorderRadius.circular(8),
          ),
          child: DropdownButtonHideUnderline(
            child: DropdownButton<String>(
              isExpanded: true,
              value: _availableModels.any((m) => m.modelRunId == appState.modelRunId)
                  ? appState.modelRunId
                  : null,
              hint: const Text('Choose a model'),
              dropdownColor: const Color(0xFF020617),
              items: _availableModels.map((m) {
                return DropdownMenuItem(
                  value: m.modelRunId,
                  child: Text('${m.name} (${m.status})', style: const TextStyle(fontSize: 14)),
                );
              }).toList(),
              onChanged: (val) {
                if (val != null) {
                  appState.setModel(val);
                  _syncWithAppState();
                }
              },
            ),
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final currentDataset = appState.currentDataset;
    final currentModel = appState.currentModel;

    final canTrain = currentDataset?.status == 'SUCCEEDED' || currentDataset?.status == 'COMPLETED';
    final canInfer = currentModel?.status == 'COMPLETED';

    return SingleChildScrollView(
      padding: const EdgeInsets.all(32),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildHeader(),
          if (_selectionWarning != null)
            Padding(
              padding: const EdgeInsets.only(top: 16),
              child: InlineAlert(
                message: _selectionWarning!,
                color: Colors.amber,
                onRetry: _fetchResources,
              ),
            ),
          const SizedBox(height: 12),
          Row(
            children: [
              ElevatedButton.icon(
                onPressed: _refreshStatus,
                icon: const Icon(Icons.refresh),
                label: const Text('Refresh Status'),
              ),
              if (_fetchingResources)
                const Padding(
                  padding: EdgeInsets.only(left: 16),
                  child: SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 32),
          Wrap(
            spacing: 24,
            runSpacing: 24,
            children: [
              _buildCard(
                title: '1. Data Generation',
                child: Column(
                  children: [
                    _buildTextField('Host Count', _hostCountController),
                    const SizedBox(height: 16),
                    _buildButton('Generate Dataset', _generate, enabled: !_loading),
                    const SizedBox(height: 16),
                    const Divider(color: Colors.white24),
                    const SizedBox(height: 16),
                    _buildDatasetSelector(appState),
                    if (currentDataset != null) _buildDatasetStatus(currentDataset),
                  ],
                ),
              ),
              _buildCard(
                title: '2. Training Parameters',
                enabled: canTrain,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    if (!canTrain)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 16),
                        child: Text(
                          currentDataset == null
                              ? 'Select a dataset to enable training'
                              : 'Wait for dataset generation to complete',
                          style: const TextStyle(color: Colors.amberAccent, fontSize: 13),
                        ),
                      ),
                    
                    Container(
                      padding: const EdgeInsets.all(16),
                      decoration: BoxDecoration(
                        color: Colors.white.withValues(alpha: 0.05),
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(color: Colors.white10),
                      ),
                      child: Column(
                        children: [
                          _buildTextField('Model Name (optional)', _modelNameController),
                          const SizedBox(height: 16),
                          Row(
                            children: [
                              Expanded(
                                child: _buildTextField('N Components (1-5)', _nComponentsController),
                              ),
                              const SizedBox(width: 16),
                              Expanded(
                                child: _buildTextField('Percentile (50-99.9)', _percentileController),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 16),
                    _buildHpoPanel(),
                    const SizedBox(height: 16),
                    _buildButton('Start Training', _train, enabled: !_loading && canTrain),
                    const SizedBox(height: 16),
                    const Divider(color: Colors.white24),
                    const SizedBox(height: 16),
                    const Text('Saved Models', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, color: Colors.white70)),
                    const SizedBox(height: 8),
                    _buildModelSelector(appState),
                    if (currentModel != null) _buildModelStatus(currentModel),
                  ],
                ),
              ),
              _buildCard(
                title: '3. Inference Preview',
                enabled: canInfer,
                child: Column(
                  children: [
                    if (!canInfer)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 16),
                        child: Text(
                          currentModel == null
                              ? 'Select a model to enable inference'
                              : 'Wait for model training to complete',
                          style: const TextStyle(color: Colors.amberAccent, fontSize: 13),
                        ),
                      ),
                    if (canInfer && _datasetSamples.isNotEmpty) ...[
                      const Text('Use real sample from dataset',
                          style: TextStyle(color: Color(0xFF94A3B8), fontSize: 14)),
                      const SizedBox(height: 8),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 12),
                        decoration: BoxDecoration(
                          color: const Color(0xFF020617),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: DropdownButtonHideUnderline(
                          child: DropdownButton<int>(
                            isExpanded: true,
                            hint: const Text('Pick a record'),
                            dropdownColor: const Color(0xFF020617),
                            items: _datasetSamples.asMap().entries.map((entry) {
                              final idx = entry.key;
                              final s = entry.value;
                              return DropdownMenuItem(
                                value: idx,
                                child: Text('Host ${s['host_id'].substring(0, 8)}... @ ${s['timestamp'].substring(11, 19)}',
                                    style: const TextStyle(fontSize: 13)),
                              );
                            }).toList(),
                            onChanged: (val) {
                              if (val != null) {
                                _inferWithSample(_datasetSamples[val]);
                              }
                            },
                          ),
                        ),
                      ),
                      const SizedBox(height: 16),
                      const Text('— OR —', style: TextStyle(color: Colors.white24, fontSize: 10)),
                      const SizedBox(height: 16),
                    ],
                    if (_pendingInferenceMessage != null)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 12),
                        child: Row(
                          children: [
                            const Icon(Icons.check_circle_outline, color: Color(0xFF4ADE80), size: 16),
                            const SizedBox(width: 8),
                            Expanded(child: Text(_pendingInferenceMessage!, style: const TextStyle(color: Color(0xFF4ADE80), fontSize: 12))),
                            IconButton(
                              icon: const Icon(Icons.close, size: 14, color: Colors.white54),
                              onPressed: () => setState(() => _pendingInferenceMessage = null),
                              padding: EdgeInsets.zero,
                              constraints: const BoxConstraints(),
                            ),
                          ],
                        ),
                      ),
                    _buildButton('Test Anomaly Detection (Static)', _infer, enabled: !_loading && canInfer),
                    if (_inferenceResults != null) _buildInferenceResults(),
                  ],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildHeader() {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              ShaderMask(
                shaderCallback: (bounds) => const LinearGradient(
                  colors: [Color(0xFF38BDF8), Color(0xFF818CF8)],
                ).createShader(bounds),
                child: const Text(
                  'TADS Dashboard',
                  style: TextStyle(fontSize: 40, fontWeight: FontWeight.bold, color: Colors.white),
                ),
              ),
              const Text(
                'Telemetry Anomaly Detection System',
                style: TextStyle(fontSize: 16, color: Color(0xFF94A3B8)),
              ),
            ],
          ),
        ),
        const CopyShareLinkButton(
          label: 'Copy Link',
          tooltip: 'Copy share link',
        ),
      ],
    );
  }

  Widget _buildCard({required String title, required Widget child, bool enabled = true}) {
    return Opacity(
      opacity: enabled ? 1.0 : 0.5,
      child: Container(
        width: 380,
        padding: const EdgeInsets.all(24),
        decoration: BoxDecoration(
          color: const Color(0xFF1E293B).withValues(alpha: 0.7),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
        ),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(16),
          child: BackdropFilter(
            filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title,
                    style: const TextStyle(
                        fontSize: 20, fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
                const SizedBox(height: 24),
                child,
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildTextField(String label, TextEditingController controller) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: const TextStyle(color: Color(0xFF94A3B8), fontSize: 14)),
        const SizedBox(height: 8),
        TextField(
          controller: controller,
          decoration: InputDecoration(
            filled: true,
            fillColor: const Color(0xFF020617),
            border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide.none),
            contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          ),
        ),
      ],
    );
  }

  String _formatDatasetLabel(DatasetRun dataset) {
    final shortId = dataset.runId.length > 8 ? dataset.runId.substring(0, 8) : dataset.runId;
    final rowCount = _formatCount(dataset.insertedRows);
    final created = _formatCreatedAt(dataset.createdAt);
    final suffix = created.isNotEmpty ? ' • $created' : '';
    return '$shortId • $rowCount rows • ${dataset.status}$suffix';
  }

  String _formatCount(int value) {
    final s = value.toString();
    final buffer = StringBuffer();
    for (var i = 0; i < s.length; i++) {
      final indexFromEnd = s.length - i;
      buffer.write(s[i]);
      if (indexFromEnd > 1 && indexFromEnd % 3 == 1) {
        buffer.write(',');
      }
    }
    return buffer.toString();
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

  Widget _buildButton(String text, VoidCallback onPressed, {bool enabled = true}) {
    return SizedBox(
      width: double.infinity,
      child: ElevatedButton(
        onPressed: enabled ? onPressed : null,
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF38BDF8),
          foregroundColor: const Color(0xFF0F172A),
          padding: const EdgeInsets.symmetric(vertical: 20),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
          disabledBackgroundColor: const Color(0xFF334155),
        ),
        child: Text(text, style: const TextStyle(fontWeight: FontWeight.bold)),
      ),
    );
  }

  Widget _buildStatusText(String label, String value, Color color) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Text('$label: ', style: const TextStyle(color: Color(0xFF94A3B8))),
          Text(value, style: TextStyle(color: color, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }

  Widget _buildDatasetStatus(DatasetStatus status) {
    return Container(
      margin: const EdgeInsets.only(top: 24),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: Colors.black26, borderRadius: BorderRadius.circular(8)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildStatusText('ID', status.runId, Colors.white70),
          _buildStatusText('Status', status.status, _getStatusColor(status.status)),
          _buildStatusText('Rows', status.rowsInserted.toString(), Colors.white70),
        ],
      ),
    );
  }

  Widget _buildModelStatus(ModelStatus status) {
    return Container(
      margin: const EdgeInsets.only(top: 24),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: Colors.black26, borderRadius: BorderRadius.circular(8)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildStatusText('ID', status.modelRunId, Colors.white70),
          _buildStatusText('Status', status.status, _getStatusColor(status.status)),
          if (status.error != null) Text(status.error!, style: const TextStyle(color: Colors.red)),
        ],
      ),
    );
  }

  Widget _buildInferenceResults() {
    return Container(
      margin: const EdgeInsets.only(top: 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Divider(color: Colors.white24),
          const SizedBox(height: 16),
          const Text('Results', style: TextStyle(fontWeight: FontWeight.bold)),
          ..._inferenceResults!.results.asMap().entries.map((entry) {
            final idx = entry.key;
            final r = entry.value;
            return Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Text(
                'Sample ${idx + 1}: ${r.isAnomaly ? "ANOMALY" : "NORMAL"} (${r.score.toStringAsFixed(4)})',
                style: TextStyle(color: r.isAnomaly ? Colors.redAccent : Colors.white70),
              ),
            );
          }),
          const SizedBox(height: 16),
          Text('Found ${_inferenceResults!.anomalyCount} anomalies.',
              style: const TextStyle(color: Color(0xFF38BDF8))),
        ],
      ),
    );
  }

  Color _getStatusColor(String status) {
    switch (status) {
      case 'SUCCEEDED':
      case 'COMPLETED':
        return const Color(0xFF4ADE80);
      case 'FAILED':
        return const Color(0xFFF87171);
      case 'RUNNING':
        return const Color(0xFFFBBF24);
      default:
        return Colors.white54;
    }
  }

  Widget _buildHpoPanel() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF334155).withValues(alpha: 0.3),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: _hpoEnabled ? const Color(0xFF38BDF8).withValues(alpha: 0.5) : Colors.white10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  const Text('Hyperparameter Tuning', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, color: Colors.white70)),
                  const SizedBox(width: 4),
                  Tooltip(
                    message: 'Automatically explore parameter combinations. Grid search is deterministic; Random search can be seeded.',
                    child: Icon(Icons.help_outline, size: 14, color: Colors.white38),
                  ),
                ],
              ),
              Switch(
                value: _hpoEnabled,
                onChanged: (val) => setState(() => _hpoEnabled = val),
                activeThumbColor: const Color(0xFF38BDF8),
              ),
            ],
          ),
          if (_hpoEnabled) ...[
            const SizedBox(height: 12),
            Row(
              children: [
                const Text('Algorithm: ', style: TextStyle(color: Color(0xFF94A3B8), fontSize: 13)),
                const SizedBox(width: 8),
                DropdownButton<String>(
                  value: _hpoAlgorithm,
                  dropdownColor: const Color(0xFF020617),
                  style: const TextStyle(fontSize: 13, color: Colors.white),
                  items: ['grid', 'random'].map((a) => DropdownMenuItem(value: a, child: Text(a.toUpperCase()))).toList(),
                  onChanged: (val) => setState(() => _hpoAlgorithm = val!),
                ),
                const Spacer(),
                SizedBox(
                  width: 80,
                  child: _buildTextField('Max Trials', _hpoMaxTrialsController),
                ),
              ],
            ),
            const SizedBox(height: 12),
            _buildTextField('N Components Search Space (csv)', _hpoNComponentsSpaceController),
            const SizedBox(height: 12),
            _buildTextField('Percentile Search Space (csv)', _hpoPercentileSpaceController),
            const SizedBox(height: 8),
            const Text('Example: 2, 3, 4', style: TextStyle(color: Colors.white24, fontSize: 11)),
          ],
        ],
      ),
    );
  }
}
