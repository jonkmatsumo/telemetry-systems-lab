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
  // Only show Date if it's 00:00? No, space is tight on charts.
  // But Issue 4 says "Time Range / Timezone Explicitness".
  // The ChartCard header usually has "UTC" or "Local" pill.
  // The axis labels should be concise.
  // But let's check if we can add a subtle indicator or just rely on the card header.
  // "The dashboard shows an explicit timezone indicator (UTC or Local) on time-based charts."
  // "Charts display timezone... a global UTC/Local toggle controls formatting."
  
  // If I add " UTC" to every label, it might clutter the X-axis.
  // However, the card *pill* says "UTC" or "Local".
  // Let's stick to HH:MM but maybe add date if it crosses midnight?
  // Actually, the previous turn's prompt said "Charts display timezone...". 
  // In `dataset_analytics_screen.dart`, `pillLabels` includes `useUtc ? 'UTC' : 'Local'`.
  // So the *card* is explicit.
  // The axis label just needs to respect the shift.
  
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
