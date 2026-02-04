import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../state/app_state.dart';
import '../utils/verbose_steps.dart';
import 'verbose_stepper.dart';

const String _traceBaseUrl = String.fromEnvironment('TRACE_BASE_URL', defaultValue: '');

class AnalyticsDebugPanel extends StatefulWidget {
  final String? requestId;
  final String? paramsSummary;
  final double? durationMs;
  final bool? cacheHit;
  final String? serverTime;
  final String? traceUrlOverride;
  final List<TraceSpan>? traceSpans;

  const AnalyticsDebugPanel({
    super.key,
    this.requestId,
    this.paramsSummary,
    this.durationMs,
    this.cacheHit,
    this.serverTime,
    this.traceUrlOverride,
    this.traceSpans,
  });

  @override
  State<AnalyticsDebugPanel> createState() => _AnalyticsDebugPanelState();
}

class _AnalyticsDebugPanelState extends State<AnalyticsDebugPanel> {
  List<VerboseStep> _steps = const [];
  String _stepsKey = '';

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _ensureSteps();
  }

  @override
  void didUpdateWidget(covariant AnalyticsDebugPanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    _ensureSteps();
  }

  void _ensureSteps() {
    final spans = widget.traceSpans ?? const [];
    final key = spans.map((s) => '${s.traceId}:${s.spanId}:${s.startMs}:${s.endMs}').join('|');
    if (key == _stepsKey) return;
    _stepsKey = key;
    _steps = buildVerboseSteps(spans);
  }

  @override
  Widget build(BuildContext context) {
    if (widget.requestId == null &&
        widget.paramsSummary == null &&
        widget.durationMs == null &&
        widget.cacheHit == null &&
        widget.serverTime == null) {
      return const SizedBox.shrink();
    }
    final traceUrl = (widget.traceUrlOverride != null && widget.traceUrlOverride!.isNotEmpty)
        ? widget.traceUrlOverride
        : (widget.requestId != null && _traceBaseUrl.isNotEmpty ? '$_traceBaseUrl/${widget.requestId}' : null);
    final verboseMode = context.watch<AppState>().verboseMode;
    final verboseTraceUrl = (traceUrl != null) ? '$traceUrl?verbose=1' : null;
    return ExpansionTile(
      tilePadding: EdgeInsets.zero,
      childrenPadding: const EdgeInsets.only(left: 8, right: 8, bottom: 8),
      title: const Text('Debug', style: TextStyle(color: Colors.white60, fontSize: 12)),
      children: [
        if (widget.requestId != null) _debugRow(context, 'Request ID', widget.requestId!, copyable: true),
        if (widget.paramsSummary != null) _debugRow(context, 'Params', widget.paramsSummary!),
        if (widget.durationMs != null)
          _debugRow(context, 'Duration', '${widget.durationMs!.toStringAsFixed(1)} ms'),
        if (widget.cacheHit != null) _debugRow(context, 'Cache', widget.cacheHit! ? 'hit' : 'miss'),
        if (widget.serverTime != null) _debugRow(context, 'Server time', widget.serverTime!),
        if (traceUrl != null) _debugRow(context, 'Trace URL', traceUrl, copyable: true),
        if (verboseMode && verboseTraceUrl != null)
          _debugRow(context, 'Trace URL (Verbose)', verboseTraceUrl, copyable: true),
        if (verboseMode) ...[
          const SizedBox(height: 8),
          const Text('Verbose / Diagnostic View', style: TextStyle(color: Colors.white70, fontSize: 12)),
          if (traceUrl == null)
            const Padding(
              padding: EdgeInsets.only(top: 6),
              child: Text('Trace unavailable for this request.',
                  style: TextStyle(color: Colors.white54, fontSize: 12)),
            )
          else
            VerboseStepper(steps: _steps, traceBaseUrl: widget.traceUrlOverride ?? _traceBaseUrl),
        ],
      ],
    );
  }

  Widget _debugRow(BuildContext context, String label, String value, {bool copyable = false}) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 90,
            child: Text(label, style: const TextStyle(color: Colors.white38, fontSize: 11)),
          ),
          Expanded(
            child: SelectableText(value, style: const TextStyle(color: Colors.white70, fontSize: 11)),
          ),
          if (copyable)
            IconButton(
              tooltip: 'Copy',
              icon: const Icon(Icons.copy, size: 14, color: Colors.white54),
              onPressed: () {
                Clipboard.setData(ClipboardData(text: value));
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('Copied to clipboard')),
                );
              },
            ),
        ],
      ),
    );
  }
}
