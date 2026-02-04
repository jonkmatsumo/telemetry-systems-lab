import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../state/app_state.dart';
import '../utils/verbose_steps.dart';
import 'verbose_stepper.dart';

const String _traceBaseUrl = String.fromEnvironment('TRACE_BASE_URL', defaultValue: '');

class AnalyticsDebugPanel extends StatelessWidget {
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
  Widget build(BuildContext context) {
    if (requestId == null &&
        paramsSummary == null &&
        durationMs == null &&
        cacheHit == null &&
        serverTime == null) {
      return const SizedBox.shrink();
    }
    final traceUrl = (traceUrlOverride != null && traceUrlOverride!.isNotEmpty)
        ? traceUrlOverride
        : (requestId != null && _traceBaseUrl.isNotEmpty ? '$_traceBaseUrl/$requestId' : null);
    final verboseMode = context.watch<AppState>().verboseMode;
    final steps = buildVerboseSteps(traceSpans ?? const []);
    final verboseTraceUrl = (traceUrl != null) ? '$traceUrl?verbose=1' : null;
    return ExpansionTile(
      tilePadding: EdgeInsets.zero,
      childrenPadding: const EdgeInsets.only(left: 8, right: 8, bottom: 8),
      title: const Text('Debug', style: TextStyle(color: Colors.white60, fontSize: 12)),
      children: [
        if (requestId != null) _debugRow(context, 'Request ID', requestId!, copyable: true),
        if (paramsSummary != null) _debugRow(context, 'Params', paramsSummary!),
        if (durationMs != null) _debugRow(context, 'Duration', '${durationMs!.toStringAsFixed(1)} ms'),
        if (cacheHit != null) _debugRow(context, 'Cache', cacheHit! ? 'hit' : 'miss'),
        if (serverTime != null) _debugRow(context, 'Server time', serverTime!),
        if (traceUrl != null) _debugRow(context, 'Trace URL', traceUrl, copyable: true),
        if (verboseMode && verboseTraceUrl != null)
          _debugRow(context, 'Trace URL (Verbose)', verboseTraceUrl, copyable: true),
        if (verboseMode) ...[
          const SizedBox(height: 8),
          const Text('Verbose / Diagnostic View', style: TextStyle(color: Colors.white70, fontSize: 12)),
          VerboseStepper(steps: steps, traceBaseUrl: traceUrlOverride ?? _traceBaseUrl),
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
