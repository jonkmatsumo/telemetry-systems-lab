import 'package:flutter/material.dart';

class LineChart extends StatelessWidget {
  final List<double> x;
  final List<double> y;
  final Color lineColor;
  final double strokeWidth;

  const LineChart({
    super.key,
    required this.x,
    required this.y,
    this.lineColor = const Color(0xFF38BDF8),
    this.strokeWidth = 2.0,
  });

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      painter: _LineChartPainter(x, y, lineColor, strokeWidth),
      child: const SizedBox.expand(),
    );
  }
}

class _LineChartPainter extends CustomPainter {
  final List<double> x;
  final List<double> y;
  final Color lineColor;
  final double strokeWidth;

  _LineChartPainter(this.x, this.y, this.lineColor, this.strokeWidth);

  @override
  void paint(Canvas canvas, Size size) {
    if (x.isEmpty || y.isEmpty || x.length != y.length) return;

    final minX = x.reduce((a, b) => a < b ? a : b);
    final maxX = x.reduce((a, b) => a > b ? a : b);
    final minY = y.reduce((a, b) => a < b ? a : b);
    final maxY = y.reduce((a, b) => a > b ? a : b);

    final dx = (maxX - minX) == 0 ? 1.0 : (maxX - minX);
    final dy = (maxY - minY) == 0 ? 1.0 : (maxY - minY);

    final path = Path();
    for (int i = 0; i < x.length; i++) {
      final px = (x[i] - minX) / dx * size.width;
      final py = size.height - (y[i] - minY) / dy * size.height;
      if (i == 0) {
        path.moveTo(px, py);
      } else {
        path.lineTo(px, py);
      }
    }

    final paint = Paint()
      ..color = lineColor
      ..strokeWidth = strokeWidth
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant _LineChartPainter oldDelegate) {
    return oldDelegate.x != x || oldDelegate.y != y || oldDelegate.lineColor != lineColor;
  }
}

class BarChart extends StatelessWidget {
  final List<double> values;
  final Color barColor;

  const BarChart({
    super.key,
    required this.values,
    this.barColor = const Color(0xFF818CF8),
  });

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      painter: _BarChartPainter(values, barColor),
      child: const SizedBox.expand(),
    );
  }
}

class _BarChartPainter extends CustomPainter {
  final List<double> values;
  final Color barColor;

  _BarChartPainter(this.values, this.barColor);

  @override
  void paint(Canvas canvas, Size size) {
    if (values.isEmpty) return;
    final maxV = values.reduce((a, b) => a > b ? a : b);
    final barWidth = size.width / values.length;

    final paint = Paint()
      ..color = barColor
      ..style = PaintingStyle.fill;

    for (int i = 0; i < values.length; i++) {
      final h = maxV == 0 ? 0 : (values[i] / maxV) * size.height;
      final rect = Rect.fromLTWH(i * barWidth, size.height - h, barWidth * 0.9, h.toDouble());
      canvas.drawRRect(RRect.fromRectAndRadius(rect, const Radius.circular(4)), paint);
    }
  }

  @override
  bool shouldRepaint(covariant _BarChartPainter oldDelegate) {
    return oldDelegate.values != values || oldDelegate.barColor != barColor;
  }
}

class ChartCard extends StatelessWidget {
  final String title;
  final Widget child;
  final double height;

  const ChartCard({
    super.key,
    required this.title,
    required this.child,
    this.height = 220,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.2),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(color: Colors.white70, fontWeight: FontWeight.bold)),
          const SizedBox(height: 12),
          SizedBox(height: height, child: child),
        ],
      ),
    );
  }
}
