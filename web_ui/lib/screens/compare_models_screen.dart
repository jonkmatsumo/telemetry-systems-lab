import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../widgets/copy_share_link_button.dart';

class CompareModelsScreen extends StatefulWidget {
  final String leftRunId;
  final String? rightRunId;

  const CompareModelsScreen({
    super.key,
    required this.leftRunId,
    this.rightRunId,
  });

  @override
  State<CompareModelsScreen> createState() => _CompareModelsScreenState();
}

class _CompareModelsScreenState extends State<CompareModelsScreen> {
  Map<String, dynamic>? _leftDetail;
  Map<String, dynamic>? _rightDetail;
  bool _loading = false;
  List<ModelRunSummary> _availableModels = [];
  String? _rightRunId;

  @override
  void initState() {
    super.initState();
    _rightRunId = widget.rightRunId;
    _fetchData();
  }

  Future<void> _fetchData() async {
    setState(() => _loading = true);
    final service = context.read<TelemetryService>();
    try {
      final left = await service.getModelDetail(widget.leftRunId);
      Map<String, dynamic>? right;
      if (_rightRunId != null) {
        right = await service.getModelDetail(_rightRunId!);
      }
      final models = await service.listModels(limit: 100);
      
      if (!mounted) return;
      setState(() {
        _leftDetail = left;
        _rightDetail = right;
        _availableModels = models;
        _loading = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _loading = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error loading comparison: $e'), backgroundColor: Colors.red),
      );
    }
  }

  Future<void> _updateRight(String? id) async {
    if (id == null) {
      setState(() {
        _rightRunId = null;
        _rightDetail = null;
      });
      return;
    }
    setState(() {
      _rightRunId = id;
      _loading = true;
    });
    final service = context.read<TelemetryService>();
    try {
      final right = await service.getModelDetail(id);
      if (!mounted) return;
      setState(() {
        _rightDetail = right;
        _loading = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _loading = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error loading model: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Compare Training Runs'),
        actions: [
          if (_leftDetail != null && _rightDetail != null)
            Padding(
              padding: const EdgeInsets.only(right: 16),
              child: CopyShareLinkButton(
                label: 'Share Comparison',
                overrideParams: {
                  'compareLeft': _leftDetail!['model_run_id'],
                  'compareRight': _rightDetail!['model_run_id'],
                },
              ),
            ),
        ],
      ),
      body: _loading && _leftDetail == null
          ? const Center(child: CircularProgressIndicator())
          : Column(
              children: [
                Expanded(
                  child: Row(
                    children: [
                      Expanded(child: _buildModelColumn(_leftDetail, isLeft: true)),
                      const VerticalDivider(width: 1, color: Colors.white24),
                      Expanded(child: _buildModelColumn(_rightDetail, isLeft: false)),
                    ],
                  ),
                ),
                _buildDiffFooter(),
              ],
            ),
    );
  }

  Widget _buildModelColumn(Map<String, dynamic>? detail, {required bool isLeft}) {
    if (detail == null && !isLeft) {
      return _buildModelSelector();
    }
    if (detail == null) return const Center(child: CircularProgressIndicator());

    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(isLeft ? 'Left Model' : 'Right Model',
                style: const TextStyle(fontSize: 14, color: Color(0xFF94A3B8))),
            if (!isLeft)
              TextButton(
                onPressed: () => _updateRight(null),
                child: const Text('Change'),
              ),
          ],
        ),
        Text(detail['name'] ?? 'N/A',
            style: const TextStyle(fontSize: 24, fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
        const SizedBox(height: 8),
        Text(detail['model_run_id'], style: const TextStyle(fontSize: 10, color: Colors.white54)),
        const SizedBox(height: 24),
        _section('Configuration'),
        _kv('Status', detail['status']),
        _kv('Created', _formatTs(detail['created_at'])),
        _kv('Dataset', '${(detail['dataset_id'] ?? '').toString().substring(0, 8)}...'),
        
        // Training Config
        ..._buildConfigRows(detail['training_config'] ?? {}),

        const SizedBox(height: 24),
        _section('Artifact Stats'),
        _kv('Artifact Path', detail['artifact_path'] ?? 'N/A'),
        _kv('N Components', (detail['artifact']?['model']?['n_components'] ?? 'N/A').toString()),
        _kv('Threshold', (detail['artifact']?['thresholds']?['reconstruction_error'] ?? 'N/A').toString()),
      ],
    );
  }

  List<Widget> _buildConfigRows(Map<String, dynamic> config) {
    if (config.isEmpty) return [const Text('No training config stored.')];
    return config.entries.map((e) => _kv(e.key, e.value.toString())).toList();
  }

  Widget _buildModelSelector() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Text('Select a model to compare', style: TextStyle(fontSize: 18, color: Colors.white70)),
          const SizedBox(height: 24),
          SizedBox(
            width: 300,
            child: DropdownButton<String>(
              isExpanded: true,
              hint: const Text('Choose model'),
              items: _availableModels.where((m) => m.modelRunId != widget.leftRunId).map((m) {
                return DropdownMenuItem(
                  value: m.modelRunId,
                  child: Text('${m.name} (${m.status})'),
                );
              }).toList(),
              onChanged: _updateRight,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDiffFooter() {
    if (_leftDetail == null || _rightDetail == null) return const SizedBox.shrink();
    
    // Simple comparison of key metrics if available
    final leftT = _leftDetail!['artifact']?['thresholds']?['reconstruction_error'] ?? 0.0;
    final rightT = _rightDetail!['artifact']?['thresholds']?['reconstruction_error'] ?? 0.0;
    final tDiff = rightT - leftT;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: const BoxDecoration(
        color: Color(0xFF0F172A),
        border: Border(top: BorderSide(color: Colors.white24)),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Text('Threshold Delta: ', style: TextStyle(color: Colors.white54)),
          Text(
            '${tDiff > 0 ? "+" : ""}${tDiff.toStringAsFixed(6)}',
            style: TextStyle(
              fontWeight: FontWeight.bold,
              color: tDiff == 0 ? Colors.white : (tDiff > 0 ? Colors.orangeAccent : Colors.greenAccent),
            ),
          ),
        ],
      ),
    );
  }

  Widget _section(String title) {
    return Padding(
      padding: const EdgeInsets.only(top: 16, bottom: 8),
      child: Text(title, style: const TextStyle(fontWeight: FontWeight.bold, color: Colors.white70)),
    );
  }

  Widget _kv(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          SizedBox(width: 120, child: Text(label, style: const TextStyle(color: Colors.white54, fontSize: 13))),
          Expanded(child: Text(value, style: const TextStyle(fontSize: 13))),
        ],
      ),
    );
  }

  String _formatTs(String? raw) {
    if (raw == null || raw.isEmpty) return 'N/A';
    final parsed = DateTime.tryParse(raw);
    if (parsed == null) return raw;
    return '${parsed.year}-${parsed.month.toString().padLeft(2, '0')}-${parsed.day.toString().padLeft(2, '0')} ${parsed.hour.toString().padLeft(2, '0')}:${parsed.minute.toString().padLeft(2, '0')}';
  }
}
