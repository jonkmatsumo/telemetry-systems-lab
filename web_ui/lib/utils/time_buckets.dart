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
