import 'package:flutter/material.dart';

enum AnalyticsState {
  loading,
  empty,
  error,
  partial,
}

class AnalyticsStatePanel extends StatelessWidget {
  final AnalyticsState state;
  final String title;
  final String message;
  final VoidCallback? onRetry;
  final String? detail;

  const AnalyticsStatePanel({
    super.key,
    required this.state,
    required this.title,
    required this.message,
    this.onRetry,
    this.detail,
  });

  @override
  Widget build(BuildContext context) {
    final Color accent;
    final IconData icon;
    switch (state) {
      case AnalyticsState.loading:
        accent = Colors.blueGrey;
        icon = Icons.hourglass_top;
        break;
      case AnalyticsState.empty:
        accent = Colors.blueGrey;
        icon = Icons.inbox_outlined;
        break;
      case AnalyticsState.partial:
        accent = Colors.orangeAccent;
        icon = Icons.warning_amber_rounded;
        break;
      case AnalyticsState.error:
        accent = Colors.redAccent;
        icon = Icons.error_outline;
        break;
    }

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: accent.withOpacity(0.08),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: accent.withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: accent, size: 18),
              const SizedBox(width: 8),
              Text(title, style: TextStyle(color: accent, fontWeight: FontWeight.bold)),
            ],
          ),
          const SizedBox(height: 8),
          Text(message, style: const TextStyle(color: Colors.white70, fontSize: 13)),
          if (detail != null) ...[
            const SizedBox(height: 6),
            Text(detail!, style: const TextStyle(color: Colors.white54, fontSize: 11)),
          ],
          if (onRetry != null && state == AnalyticsState.error) ...[
            const SizedBox(height: 12),
            TextButton.icon(
              onPressed: onRetry,
              icon: Icon(Icons.refresh, size: 16, color: accent),
              label: Text('Retry', style: TextStyle(color: accent)),
              style: TextButton.styleFrom(padding: EdgeInsets.zero, minimumSize: const Size(0, 0)),
            ),
          ],
        ],
      ),
    );
  }
}
