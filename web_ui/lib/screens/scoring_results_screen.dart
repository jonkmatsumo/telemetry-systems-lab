import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/copy_share_link_button.dart';
import '../widgets/inline_alert.dart';

class ScoringResultsScreen extends StatefulWidget {
  final String datasetId;
  final String modelRunId;
  final double? initialMinScore;
  final bool initialOnlyAnomalies;

  const ScoringResultsScreen({
    super.key,
    required this.datasetId,
    required this.modelRunId,
    this.initialMinScore,
    this.initialOnlyAnomalies = false,
  });

  @override
  State<ScoringResultsScreen> createState() => _ScoringResultsScreenState();
}

class _ScoringResultsScreenState extends State<ScoringResultsScreen> {
  int _offset = 0;
  final int _limit = 50;
  bool _onlyAnomalies = false;
  double _minScore = 0.0;
  double _maxScore = 10.0;
  
  Future<Map<String, dynamic>>? _resultsFuture;

  @override
  void initState() {
    super.initState();
    _minScore = widget.initialMinScore ?? 0.0;
    _onlyAnomalies = widget.initialOnlyAnomalies;
    _load();
  }

  void _load() {
    setState(() {
      _resultsFuture = context.read<TelemetryService>().getScores(
        widget.datasetId,
        widget.modelRunId,
        limit: _limit,
        offset: _offset,
        onlyAnomalies: _onlyAnomalies,
        minScore: _minScore,
      ).then((data) {
        if (mounted && data.containsKey('max_score')) {
           // Defer state update to avoid build conflict during build frame
           WidgetsBinding.instance.addPostFrameCallback((_) {
             if (mounted) {
               setState(() {
                 _maxScore = (data['max_score'] as num).toDouble();
                 if (_maxScore < 10.0) _maxScore = 10.0; // Keep at least 10 for visibility
                 if (_minScore > _maxScore) _minScore = _maxScore;
               });
             }
           });
        }
        return data;
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0F172A),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1E293B),
        title: Text('Scoring Results — ${widget.modelRunId.substring(0, 8)} on ${widget.datasetId.substring(0, 8)}'),
        actions: [
          CopyShareLinkButton(
            showLabel: false,
            tooltip: 'Copy results link',
            overrideParams: {
              'resultsDatasetId': widget.datasetId,
              'resultsModelId': widget.modelRunId,
              'minScore': _minScore.toString(),
              'onlyAnomalies': _onlyAnomalies.toString(),
            },
          ),
          const SizedBox(width: 16),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          children: [
            _buildFilters(),
            const SizedBox(height: 24),
            Expanded(child: _buildTable()),
          ],
        ),
      ),
    );
  }

  Widget _buildFilters() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.05),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          FilterChip(
            label: const Text('Only Anomalies'),
            selected: _onlyAnomalies,
            onSelected: (val) {
              setState(() {
                _onlyAnomalies = val;
                _offset = 0;
                _load();
              });
            },
          ),
          const SizedBox(width: 24),
          const Text('Min Score: ', style: TextStyle(color: Colors.white60)),
          Expanded(
            child: Slider(
              value: _minScore,
              min: 0,
              max: _maxScore, 
              onChanged: (val) {
                setState(() => _minScore = val);
              },
              onChangeEnd: (val) {
                setState(() {
                  _offset = 0;
                  _load();
                });
              },
            ),
          ),
          Text('${_minScore.toStringAsFixed(2)} / ${_maxScore.toStringAsFixed(2)}', style: const TextStyle(fontWeight: FontWeight.bold)),
          const SizedBox(width: 24),
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _load,
          ),
        ],
      ),
    );
  }

  Widget _buildTable() {
    return FutureBuilder<Map<String, dynamic>>(
      future: _resultsFuture,
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Center(child: CircularProgressIndicator());
        }
        if (snapshot.hasError) {
          return InlineAlert(message: 'Error loading results: ${snapshot.error}');
        }
        final data = snapshot.data!;
        final items = data['items'] as List? ?? [];
        final total = data['total'] ?? 0;

        if (items.isEmpty) {
          return const Center(child: Text('No scoring results found for this selection.'));
        }

        return Column(
          children: [
            Expanded(
              child: SingleChildScrollView(
                scrollDirection: Axis.vertical,
                child: DataTable(
                  columns: const [
                    DataColumn(label: Text('Record ID')),
                    DataColumn(label: Text('Host')),
                    DataColumn(label: Text('Timestamp')),
                    DataColumn(label: Text('Score')),
                    DataColumn(label: Text('Pred')),
                    DataColumn(label: Text('Label')),
                  ],
                  rows: items.map((item) {
                    final isAnomaly = item['is_anomaly'] == true;
                    return DataRow(
                      onSelectChanged: (_) => _showDetail(item),
                      cells: [
                        DataCell(Text(item['record_id'].toString())),
                        DataCell(Text('${item['host_id'].toString().substring(0, 8)}...')),
                        DataCell(Text(item['timestamp'].toString().substring(11, 19))),
                        DataCell(Text(item['score'].toStringAsFixed(4),
                            style: TextStyle(
                                color: isAnomaly ? Colors.redAccent : Colors.greenAccent,
                                fontWeight: FontWeight.bold))),
                        DataCell(Text(isAnomaly ? 'ANOMALY' : 'NORMAL')),
                        DataCell(Text(item['label'] == true ? 'ANOMALY' : 'NORMAL')),
                      ],
                    );
                  }).toList(),
                ),
              ),
            ),
            _buildPagination(total),
          ],
        );
      },
    );
  }

  void _showDetail(Map<String, dynamic> item) async {
    final recordId = item['record_id'];
    final runId = widget.datasetId;
    
    // Show loading drawer first
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xFF1E293B),
      shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(20))),
      builder: (context) => const Center(child: CircularProgressIndicator()),
    );

    try {
      final record = await context.read<TelemetryService>().getDatasetRecord(runId, recordId);
      if (!mounted) return;
      Navigator.pop(context); // Close loading sheet

      showModalBottomSheet(
        context: context,
        backgroundColor: const Color(0xFF1E293B),
        isScrollControlled: true,
        shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(20))),
        builder: (context) {
          return Container(
            padding: const EdgeInsets.all(24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text('Record $recordId Detail',
                        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                    IconButton(icon: const Icon(Icons.close), onPressed: () => Navigator.pop(context)),
                  ],
                ),
                const SizedBox(height: 16),
                _detailRow('Host ID', record['host_id'] ?? item['host_id']),
                _detailRow('Timestamp', record['timestamp'] ?? item['timestamp']),
                _detailRow('Anomaly Score', item['score'].toStringAsFixed(6)),
                _detailRow('Prediction', item['is_anomaly'] ? 'ANOMALY' : 'NORMAL'),
                _detailRow('Ground Truth', item['label'] ? 'ANOMALY' : 'NORMAL'),
                const Divider(height: 32, color: Colors.white12),
                const Text('Raw Metrics', style: TextStyle(fontWeight: FontWeight.bold, color: Color(0xFF38BDF8))),
                const SizedBox(height: 12),
                _detailRow('CPU Usage', '${record['cpu_usage']?.toStringAsFixed(2)}%'),
                _detailRow('Memory Usage', '${record['memory_usage']?.toStringAsFixed(2)}%'),
                _detailRow('Disk Utilization', '${record['disk_utilization']?.toStringAsFixed(2)}%'),
                _detailRow('Network RX', '${record['network_rx_rate']?.toStringAsFixed(2)} Mbps'),
                _detailRow('Network TX', '${record['network_tx_rate']?.toStringAsFixed(2)} Mbps'),
                const SizedBox(height: 24),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    onPressed: () {
                      final appState = context.read<AppState>();
                      appState.setDataset(widget.datasetId);
                      appState.setModel(widget.modelRunId);
                      
                      // Pass the full record payload so we don't need to re-fetch
                      appState.setPendingInference(PendingInferenceRequest(
                        datasetId: widget.datasetId,
                        modelId: widget.modelRunId,
                        recordId: recordId.toString(),
                        recordPayload: record,
                      ));
                      
                      appState.setTabIndex(0); // Go to Control/Inference
                      Navigator.pop(context); // Close bottom sheet
                      Navigator.pop(context); // Go back from results browser
                    },
                    icon: const Icon(Icons.play_arrow),
                    label: const Text('Load into Inference Preview'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF38BDF8),
                      foregroundColor: const Color(0xFF0F172A),
                      padding: const EdgeInsets.symmetric(vertical: 16),
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      );
    } catch (e) {
      if (!mounted) return;
      Navigator.pop(context); // Close loading sheet
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Failed to load record details: $e')));
    }
  }

  Widget _detailRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: const TextStyle(color: Colors.white54)),
          Text(value, style: const TextStyle(fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }

  Widget _buildPagination(int total) {
    return Padding(
      padding: const EdgeInsets.only(top: 16),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          IconButton(
            icon: const Icon(Icons.chevron_left),
            onPressed: _offset > 0 ? () {
              setState(() {
                _offset = (_offset - _limit).clamp(0, total);
                _load();
              });
            } : null,
          ),
          Text('Page ${(_offset / _limit).floor() + 1} of ${(total / _limit).ceil()}'),
          IconButton(
            icon: const Icon(Icons.chevron_right),
            onPressed: (_offset + _limit) < total ? () {
              setState(() {
                _offset += _limit;
                _load();
              });
            } : null,
          ),
        ],
      ),
    );
  }
}
