import 'package:flutter/material.dart';

class InlineAlert extends StatelessWidget {
  final String message;
  final String? title;
  final VoidCallback? onRetry;
  final Color color;

  const InlineAlert({
    super.key,
    required this.message,
    this.title,
    this.onRetry,
    this.color = Colors.redAccent,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.error_outline, color: color, size: 20),
              const SizedBox(width: 8),
              if (title != null)
                Text(title!, style: TextStyle(color: color, fontWeight: FontWeight.bold)),
            ],
          ),
          const SizedBox(height: 8),
          Text(message, style: const TextStyle(color: Colors.white70, fontSize: 13)),
          if (onRetry != null) ...[
            const SizedBox(height: 12),
            TextButton.icon(
              onPressed: onRetry,
              icon: Icon(Icons.refresh, size: 16, color: color),
              label: Text('Retry', style: TextStyle(color: color)),
              style: TextButton.styleFrom(padding: EdgeInsets.zero, minimumSize: const Size(0, 0)),
            ),
          ],
        ],
      ),
    );
  }
}
