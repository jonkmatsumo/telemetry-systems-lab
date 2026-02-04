import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/app_state.dart';
import '../widgets/copy_share_link_button.dart';

class ContextBar extends StatelessWidget {
  final bool showCopyLink;

  const ContextBar({super.key, this.showCopyLink = true});

  @override
  Widget build(BuildContext context) {
    final appState = context.watch<AppState>();
    final datasetId = appState.datasetId;
    final modelId = appState.modelRunId;
    final datasetStatus = appState.currentDataset;
    final modelStatus = appState.currentModel;

    final datasetLabel = datasetId == null
        ? 'No dataset selected'
        : 'Dataset: ${_shortId(datasetId)}${_rowsSuffix(datasetStatus?.rowsInserted)}';
    final modelLabel = modelId == null
        ? 'No model selected'
        : 'Model: ${_modelNameOrId(modelStatus?.name, modelId)}${_statusSuffix(modelStatus?.status)}';

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      decoration: BoxDecoration(
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white12),
      ),
      child: Row(
        children: [
          Expanded(
            child: Wrap(
              spacing: 12,
              runSpacing: 8,
              children: [
                _buildChip(
                  context,
                  label: datasetLabel,
                  enabled: datasetId != null,
                  onTap: datasetId == null
                      ? null
                      : () {
                          appState.setTabIndex(2);
                        },
                ),
                _buildChip(
                  context,
                  label: modelLabel,
                  enabled: modelId != null,
                  onTap: modelId == null
                      ? null
                      : () {
                          appState.setTabIndex(3);
                        },
                ),
              ],
            ),
          ),
          if (showCopyLink)
            CopyShareLinkButton(
              showLabel: false,
              tooltip: 'Copy share link',
            ),
        ],
      ),
    );
  }

  Widget _buildChip(BuildContext context, {required String label, required bool enabled, VoidCallback? onTap}) {
    return ActionChip(
      label: Text(label),
      onPressed: enabled ? onTap : null,
      backgroundColor: enabled ? const Color(0xFF0F172A) : const Color(0xFF1E293B),
      labelStyle: TextStyle(color: enabled ? Colors.white : Colors.white54),
      shape: StadiumBorder(side: BorderSide(color: enabled ? Colors.white24 : Colors.white12)),
    );
  }

  String _shortId(String id) {
    if (id.length <= 8) return id;
    return id.substring(0, 8);
  }

  String _modelNameOrId(String? name, String id) {
    if (name != null && name.isNotEmpty) return name;
    return _shortId(id);
  }

  String _rowsSuffix(int? rows) {
    if (rows == null || rows <= 0) return '';
    return ' • ${_formatCount(rows)} rows';
  }

  String _statusSuffix(String? status) {
    if (status == null || status.isEmpty) return '';
    return ' • $status';
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
}
