import 'dart:convert';

class TraceSpan {
  final String spanId;
  final String traceId;
  final String name;
  final String status;
  final double startMs;
  final double endMs;
  final Map<String, dynamic> attributes;
  final Map<String, dynamic>? telemetry;

  TraceSpan({
    required this.spanId,
    required this.traceId,
    required this.name,
    required this.status,
    required this.startMs,
    required this.endMs,
    this.attributes = const {},
    this.telemetry,
  });
}

class VerboseArtifact {
  final String label;
  final String payload;
  final bool redacted;

  VerboseArtifact({required this.label, required this.payload, this.redacted = false});
}

class VerboseStep {
  final String id;
  final String title;
  final String status;
  final double offsetMs;
  final double durationMs;
  final List<VerboseArtifact> artifacts;
  final String? traceId;
  final String? spanId;

  VerboseStep({
    required this.id,
    required this.title,
    required this.status,
    required this.offsetMs,
    required this.durationMs,
    required this.artifacts,
    this.traceId,
    this.spanId,
  });
}

List<VerboseStep> buildVerboseSteps(List<TraceSpan> spans) {
  if (spans.isEmpty) return [];
  final sorted = List<TraceSpan>.from(spans)..sort((a, b) => a.startMs.compareTo(b.startMs));
  final base = sorted.first.startMs;
  return sorted.map((span) {
    return VerboseStep(
      id: span.spanId,
      title: _spanTitle(span.name),
      status: span.status,
      offsetMs: span.startMs - base,
      durationMs: (span.endMs - span.startMs).clamp(0, double.infinity),
      artifacts: _extractArtifacts(span),
      traceId: span.traceId,
      spanId: span.spanId,
    );
  }).toList();
}

String _spanTitle(String name) {
  const mapping = {
    'agent.plan': 'Plan',
    'agent.tool': 'Tool Call',
    'agent.respond': 'Response',
    'llm.request': 'LLM Request',
    'llm.response': 'LLM Response',
  };
  return mapping[name] ?? name;
}

List<VerboseArtifact> _extractArtifacts(TraceSpan span) {
  final artifacts = <VerboseArtifact>[];
  final telemetry = span.telemetry ?? _asMap(span.attributes['telemetry']);
  if (telemetry != null) {
    _addArtifact(artifacts, 'Inputs', telemetry['inputs_json']);
    _addArtifact(artifacts, 'Outputs', telemetry['outputs_json']);
    _addArtifact(artifacts, 'Prompt', telemetry['prompt']);
    _addArtifact(artifacts, 'Response', telemetry['response']);
  }

  _addArtifact(artifacts, 'Attributes', span.attributes);
  return artifacts;
}

void _addArtifact(List<VerboseArtifact> artifacts, String label, dynamic payload) {
  if (payload == null) return;
  final payloadText = _stringifyPayload(payload);
  if (payloadText.trim().isEmpty) return;
  artifacts.add(VerboseArtifact(label: label, payload: payloadText));
}

Map<String, dynamic>? _asMap(dynamic value) {
  if (value is Map<String, dynamic>) return value;
  if (value is Map) {
    return value.map((key, val) => MapEntry(key.toString(), val));
  }
  return null;
}

String _stringifyPayload(dynamic payload) {
  if (payload is String) return payload;
  try {
    return const JsonEncoder.withIndent('  ').convert(payload);
  } catch (_) {
    return payload.toString();
  }
}
