DateTime? parseIsoUtcOrLocal(String raw) {
  try {
    return DateTime.parse(raw);
  } catch (_) {
    return null;
  }
}

DateTime? bucketEndFromIso(String startIso, int bucketSeconds) {
  final start = parseIsoUtcOrLocal(startIso);
  if (start == null) return null;
  return start.add(Duration(seconds: bucketSeconds));
}

String formatBucketLabel(DateTime dt, {required bool useUtc}) {
  final display = useUtc ? dt.toUtc() : dt.toLocal();
  final hh = display.hour.toString().padLeft(2, '0');
  final mm = display.minute.toString().padLeft(2, '0');
  return '$hh:$mm';
}

String selectBucketLabel(Duration range) {
  final seconds = range.inSeconds;
  if (seconds <= 6 * 3600) return '5m';
  if (seconds <= 2 * 86400) return '1h';
  if (seconds <= 30 * 86400) return '6h';
  if (seconds <= 180 * 86400) return '1d';
  return '7d';
}

int bucketSecondsForLabel(String label) {
  switch (label) {
    case '5m':
      return 300;
    case '1h':
      return 3600;
    case '6h':
      return 21600;
    case '1d':
      return 86400;
    case '7d':
      return 604800;
    default:
      return 3600;
  }
}
