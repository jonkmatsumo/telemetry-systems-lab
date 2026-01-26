import 'dart:async';
import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'services/telemetry_service.dart';

void main() {
  runApp(
    Provider<TelemetryService>(
      create: (_) => TelemetryService(),
      child: const TadsApp(),
    ),
  );
}

class TadsApp extends StatelessWidget {
  const TadsApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'TADS Dashboard',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0F172A),
        primarySwatch: Colors.blue,
      ),
      home: const DashboardPage(),
    );
  }
}

class DashboardPage extends StatefulWidget {
  const DashboardPage({super.key});

  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  final _hostCountController = TextEditingController(text: '5');
  final _modelNameController = TextEditingController(text: 'pca_v1');
  
  bool _loading = false;
  DatasetStatus? _currentDataset;
  ModelStatus? _currentModel;
  InferenceResponse? _inferenceResults;
  Timer? _pollingTimer;

  @override
  void dispose() {
    _pollingTimer?.cancel();
    _hostCountController.dispose();
    _modelNameController.dispose();
    super.dispose();
  }

  void _startPolling(String id, String type) {
    _pollingTimer?.cancel();
    _pollingTimer = Timer.periodic(const Duration(seconds: 2), (timer) async {
      final service = context.read<TelemetryService>();
      try {
        if (type == 'dataset') {
          final status = await service.getDatasetStatus(id);
          setState(() => _currentDataset = status);
          if (status.status != 'PENDING' && status.status != 'RUNNING') {
            timer.cancel();
            setState(() => _loading = false);
          }
        } else {
          final status = await service.getModelStatus(id);
          setState(() => _currentModel = status);
          if (status.status != 'PENDING' && status.status != 'RUNNING') {
            timer.cancel();
            setState(() => _loading = false);
          }
        }
      } catch (e) {
        timer.cancel();
        setState(() => _loading = false);
        _showError(e.toString());
      }
    });
  }

  void _generate() async {
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final count = int.parse(_hostCountController.text);
      final runId = await service.generateDataset(count);
      _startPolling(runId, 'dataset');
    } catch (e) {
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _train() async {
    if (_currentDataset == null) return;
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final modelId = await service.trainModel(
        _currentDataset!.runId, 
        name: _modelNameController.text
      );
      _startPolling(modelId, 'model');
    } catch (e) {
      setState(() => _loading = false);
      _showError(e.toString());
    }
  }

  void _infer() async {
    if (_currentModel == null) return;
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final samples = [
        { 'cpu_usage': 98.0, 'memory_usage': 95.0, 'disk_utilization': 30.0, 'network_rx_rate': 10.0, 'network_tx_rate': 5.0 },
        { 'cpu_usage': 40.0, 'memory_usage': 50.0, 'disk_utilization': 30.0, 'network_rx_rate': 10.0, 'network_tx_rate': 5.0 }
      ];
      final res = await service.runInference(_currentModel!.modelRunId, samples);
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

  @override
  Widget build(BuildContext context) {
    final canTrain = _currentDataset?.status == 'SUCCEEDED' || _currentDataset?.status == 'COMPLETED';
    final canInfer = _currentModel?.status == 'COMPLETED';

    return Scaffold(
      body: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [Color(0xFF0F172A), Color(0xFF1E293B)],
          ),
        ),
        child: SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(32),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _buildHeader(),
                const SizedBox(height: 48),
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
                          if (_currentDataset != null) _buildDatasetStatus(),
                        ],
                      ),
                    ),
                    _buildCard(
                      title: '2. Model Training',
                      enabled: canTrain,
                      child: Column(
                        children: [
                          _buildTextField('Model Name', _modelNameController),
                          const SizedBox(height: 16),
                          _buildButton('Start Training', _train, enabled: !_loading && canTrain),
                          if (_currentModel != null) _buildModelStatus(),
                        ],
                      ),
                    ),
                    _buildCard(
                      title: '3. Inference Preview',
                      enabled: canInfer,
                      child: Column(
                        children: [
                          _buildButton('Test Anomaly Detection', _infer, enabled: !_loading && canInfer),
                          if (_inferenceResults != null) _buildInferenceResults(),
                        ],
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildHeader() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ShaderMask(
          shaderCallback: (bounds) => const LinearGradient(
            colors: [Color(0xFF38BDF8), Color(0xFF818CF8)],
          ).createShader(bounds),
          child: const Text(
            'TADS Dashboard',
            style: TextStyle(fontSize: 48, fontWeight: FontWeight.bold, color: Colors.white),
          ),
        ),
        const Text(
          'Telemetry Anomaly Detection System',
          style: TextStyle(fontSize: 18, color: Color(0xFF94A3B8)),
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
          color: const Color(0x1E293B).withOpacity(0.7),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: Colors.white.withOpacity(0.1)),
        ),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(16),
          child: BackdropFilter(
            filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
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

  Widget _buildDatasetStatus() {
    return Container(
      margin: const EdgeInsets.only(top: 24),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: Colors.black26, borderRadius: BorderRadius.circular(8)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildStatusText('ID', _currentDataset!.runId, Colors.white70),
          _buildStatusText('Status', _currentDataset!.status, _getStatusColor(_currentDataset!.status)),
          _buildStatusText('Rows', _currentDataset!.rowsInserted.toString(), Colors.white70),
        ],
      ),
    );
  }

  Widget _buildModelStatus() {
    return Container(
      margin: const EdgeInsets.only(top: 24),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: Colors.black26, borderRadius: BorderRadius.circular(8)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildStatusText('ID', _currentModel!.modelRunId, Colors.white70),
          _buildStatusText('Status', _currentModel!.status, _getStatusColor(_currentModel!.status)),
          if (_currentModel!.error != null) Text(_currentModel!.error!, style: const TextStyle(color: Colors.red)),
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
          Text('Found ${_inferenceResults!.anomalyCount} anomalies.', style: const TextStyle(color: Color(0xFF38BDF8))),
        ],
      ),
    );
  }

  Color _getStatusColor(String status) {
    switch (status) {
      case 'SUCCEEDED':
      case 'COMPLETED': return Color(0xFF4ADE80);
      case 'FAILED': return Color(0xFFF87171);
      case 'RUNNING': return Color(0xFFFBBF24);
      default: return Colors.white54;
    }
  }
}
