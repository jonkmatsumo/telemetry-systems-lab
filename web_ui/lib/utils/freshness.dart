class WidgetFreshness {
  final DateTime? requestStart;
  final DateTime? requestEnd;
  final DateTime? serverTime;
  final bool forceRefresh;
  final String? startTime;
  final String? endTime;

  const WidgetFreshness({
    this.requestStart,
    this.requestEnd,
    this.serverTime,
    this.forceRefresh = false,
    this.startTime,
    this.endTime,
  });

  WidgetFreshness copyWith({
    DateTime? requestStart,
    DateTime? requestEnd,
    DateTime? serverTime,
    bool? forceRefresh,
    String? startTime,
    String? endTime,
  }) {
    return WidgetFreshness(
      requestStart: requestStart ?? this.requestStart,
      requestEnd: requestEnd ?? this.requestEnd,
      serverTime: serverTime ?? this.serverTime,
      forceRefresh: forceRefresh ?? this.forceRefresh,
      startTime: startTime ?? this.startTime,
      endTime: endTime ?? this.endTime,
    );
  }
}

Duration? maxFreshnessDelta(Iterable<WidgetFreshness> values) {
  final times = values
      .map((f) => f.serverTime ?? f.requestEnd)
      .whereType<DateTime>()
      .map((t) => t.toUtc())
      .toList();
  if (times.length < 2) return null;
  times.sort();
  return times.last.difference(times.first);
}

bool hasMixedRefreshMode(Iterable<WidgetFreshness> values) {
  if (values.isEmpty) return false;
  final anyForced = values.any((f) => f.forceRefresh);
  final anyCached = values.any((f) => !f.forceRefresh);
  return anyForced && anyCached;
}

bool shouldShowFreshnessBanner(Iterable<WidgetFreshness> values,
    {Duration threshold = const Duration(seconds: 60)}) {
  final delta = maxFreshnessDelta(values);
  return (delta != null && delta > threshold) || hasMixedRefreshMode(values);
}
