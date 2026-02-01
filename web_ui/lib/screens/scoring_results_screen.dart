import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/telemetry_service.dart';
import '../state/app_state.dart';
import '../widgets/inline_alert.dart';

class ScoringResultsScreen extends StatefulWidget {
  final String datasetId;
  final String modelRunId;

  const ScoringResultsScreen({super.key, required this.datasetId, required this.modelRunId});

  @override
  State<ScoringResultsScreen> createState() => _ScoringResultsScreenState();
}

class _ScoringResultsScreenState extends State<ScoringResultsScreen> {
  int _offset = 0;
  final int _limit = 50;
  bool _onlyAnomalies = false;
  double _minScore = 0.0;
  
  Future<Map<String, dynamic>>? _resultsFuture;

  @override
  void initState() {
    super.initState();
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
      );
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0F172A),
      appBar: AppBar(
        backgroundColor: const Color(0xFF1E293B),
        title: Text('Scoring Results — ${widget.modelRunId.substring(0, 8)} on ${widget.datasetId.substring(0, 8)}'),
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
        color: Colors.white.withOpacity(0.05),
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
              max: 10, // Assuming a reasonable range, can be dynamic
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
          Text(_minScore.toStringAsFixed(2), style: const TextStyle(fontWeight: FontWeight.bold)),
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
                      onSelectChanged: (_) {
                        // Phase 4.3: Open detail drawer
                      },
                      cells: [
                        DataCell(Text(item['record_id'].toString())),
                        DataCell(Text(item['host_id'].toString().substring(0, 8) + '...')),
                        DataCell(Text(item['timestamp'].toString().substring(11, 19))),
                        DataCell(Text(item['score'].toStringAsFixed(4),
                            style: TextStyle(color: isAnomaly ? Colors.redAccent : Colors.greenAccent, fontWeight: FontWeight.bold))),
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
