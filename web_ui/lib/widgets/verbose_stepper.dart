import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../utils/verbose_steps.dart';

class VerboseStepper extends StatefulWidget {
  final List<VerboseStep> steps;
  final String? traceBaseUrl;

  const VerboseStepper({super.key, required this.steps, this.traceBaseUrl});

  @override
  State<VerboseStepper> createState() => _VerboseStepperState();
}

class _VerboseStepperState extends State<VerboseStepper> {
  late List<bool> _expanded;

  @override
  void initState() {
    super.initState();
    _expanded = List<bool>.filled(widget.steps.length, false);
  }

  @override
  void didUpdateWidget(covariant VerboseStepper oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.steps.length != widget.steps.length) {
      _expanded = List<bool>.filled(widget.steps.length, false);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (widget.steps.isEmpty) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 8),
        child: Text(
          'No verbose steps available for this trace.',
          style: TextStyle(color: Colors.white54, fontSize: 12),
        ),
      );
    }

    return SingleChildScrollView(
      physics: const NeverScrollableScrollPhysics(),
      child: ExpansionPanelList(
        expansionCallback: (index, isExpanded) {
          setState(() => _expanded[index] = !isExpanded);
        },
        children: [
          for (int i = 0; i < widget.steps.length; i++)
            ExpansionPanel(
              isExpanded: _expanded[i],
              canTapOnHeader: true,
              headerBuilder: (context, isExpanded) => _StepHeader(step: widget.steps[i]),
              body: _StepBody(
                step: widget.steps[i],
                traceBaseUrl: widget.traceBaseUrl,
              ),
            ),
        ],
      ),
    );
  }
}

class _StepHeader extends StatelessWidget {
  final VerboseStep step;

  const _StepHeader({required this.step});

  @override
  Widget build(BuildContext context) {
    return ListTile(
      title: Text(step.title, style: const TextStyle(color: Colors.white70, fontSize: 13)),
      subtitle: Text(
        't+${step.offsetMs.toStringAsFixed(0)}ms • ${step.durationMs.toStringAsFixed(0)}ms',
        style: const TextStyle(color: Colors.white38, fontSize: 11),
      ),
      trailing: _StatusPill(status: step.status),
    );
  }
}

class _StepBody extends StatelessWidget {
  final VerboseStep step;
  final String? traceBaseUrl;

  const _StepBody({required this.step, this.traceBaseUrl});

  @override
  Widget build(BuildContext context) {
    final traceLink = _traceLink(traceBaseUrl, step.traceId, step.spanId);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              TextButton.icon(
                onPressed: traceLink == null ? null : () => _copyLink(context, traceLink),
                icon: const Icon(Icons.account_tree, size: 16),
                label: const Text('Reveal in Waterfall'),
              ),
              const SizedBox(width: 8),
              TextButton.icon(
                onPressed: traceLink == null ? null : () => _copyLink(context, traceLink),
                icon: const Icon(Icons.open_in_new, size: 16),
                label: const Text('Open Span'),
              ),
            ],
          ),
          const SizedBox(height: 8),
          if (step.artifacts.isEmpty)
            const Text('No artifacts captured for this step.',
                style: TextStyle(color: Colors.white54, fontSize: 12))
          else
            for (final artifact in step.artifacts) _VerboseArtifactPanel(artifact: artifact),
        ],
      ),
    );
  }

  String? _traceLink(String? base, String? traceId, String? spanId) {
    if (base == null || base.isEmpty || traceId == null || traceId.isEmpty) return null;
    final spanQuery = (spanId != null && spanId.isNotEmpty) ? '?span=$spanId' : '';
    return '$base/$traceId$spanQuery';
  }

  void _copyLink(BuildContext context, String link) {
    Clipboard.setData(ClipboardData(text: link));
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Trace link copied to clipboard')),
    );
  }
}

class _VerboseArtifactPanel extends StatefulWidget {
  final VerboseArtifact artifact;

  const _VerboseArtifactPanel({required this.artifact});

  @override
  State<_VerboseArtifactPanel> createState() => _VerboseArtifactPanelState();
}

class _VerboseArtifactPanelState extends State<_VerboseArtifactPanel> {
  static const int _maxBytes = 1024;
  bool _expanded = false;

  @override
  Widget build(BuildContext context) {
    final payload = widget.artifact.payload;
    final bytes = utf8.encode(payload).length;
    final truncated = bytes > _maxBytes && !_expanded;
    final displayPayload = truncated ? _truncate(payload, _maxBytes) : payload;

    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: Colors.black.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(widget.artifact.label,
                    style: const TextStyle(color: Colors.white70, fontWeight: FontWeight.bold, fontSize: 12)),
              ),
              Text(_formatBytes(bytes), style: const TextStyle(color: Colors.white38, fontSize: 11)),
              if (widget.artifact.redacted)
                const Padding(
                  padding: EdgeInsets.only(left: 6),
                  child: _Tag(label: 'Redacted', color: Colors.orange),
                ),
              if (bytes > _maxBytes)
                const Padding(
                  padding: EdgeInsets.only(left: 6),
                  child: _Tag(label: 'Truncated', color: Colors.amber),
                ),
            ],
          ),
          const SizedBox(height: 6),
          SelectableText(
            displayPayload,
            style: const TextStyle(color: Colors.white60, fontSize: 11, fontFamily: 'monospace'),
          ),
          const SizedBox(height: 6),
          Row(
            children: [
              TextButton.icon(
                onPressed: () {
                  Clipboard.setData(ClipboardData(text: payload));
                  ScaffoldMessenger.of(context)
                      .showSnackBar(const SnackBar(content: Text('Artifact copied')));
                },
                icon: const Icon(Icons.copy, size: 14),
                label: const Text('Copy Artifact'),
              ),
              if (bytes > _maxBytes)
                TextButton(
                  onPressed: () => setState(() => _expanded = !_expanded),
                  child: Text(_expanded ? 'Show less' : 'Load more'),
                ),
            ],
          ),
        ],
      ),
    );
  }

  String _formatBytes(int bytes) {
    if (bytes < 1024) return '${bytes}B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)}KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)}MB';
  }

  String _truncate(String text, int maxBytes) {
    final bytes = utf8.encode(text);
    if (bytes.length <= maxBytes) return text;
    return '${utf8.decode(bytes.take(maxBytes).toList())}\n…';
  }
}

class _Tag extends StatelessWidget {
  final String label;
  final Color color;

  const _Tag({required this.label, required this.color});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: color.withValues(alpha: 0.4)),
      ),
      child: Text(label, style: const TextStyle(fontSize: 10, color: Colors.white70)),
    );
  }
}

class _StatusPill extends StatelessWidget {
  final String status;

  const _StatusPill({required this.status});

  @override
  Widget build(BuildContext context) {
    final color = _statusColor(status);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: color.withValues(alpha: 0.4)),
      ),
      child: Text(status.toUpperCase(), style: TextStyle(color: color, fontSize: 10)),
    );
  }

  Color _statusColor(String status) {
    switch (status.toLowerCase()) {
      case 'ok':
      case 'success':
        return Colors.greenAccent;
      case 'error':
      case 'failed':
        return Colors.redAccent;
      default:
        return Colors.orangeAccent;
    }
  }
}
