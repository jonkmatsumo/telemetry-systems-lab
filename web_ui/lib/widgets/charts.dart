import 'package:flutter/material.dart';

class LineChart extends StatelessWidget {
  final List<double> x;
  final List<double> y;
  final Color lineColor;
  final double strokeWidth;
  final String Function(double)? xLabelBuilder;
  final String Function(double)? yLabelBuilder;
  final List<bool>? partial;
  final void Function(int)? onTap;

  const LineChart({
    super.key,
    required this.x,
    required this.y,
    this.lineColor = const Color(0xFF38BDF8),
    this.strokeWidth = 2.0,
    this.xLabelBuilder,
    this.yLabelBuilder,
    this.partial,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        return GestureDetector(
          onTapUp: (details) {
            if (onTap == null || x.isEmpty) return;
            final double leftMargin = (yLabelBuilder != null) ? 48.0 : 0.0;
            final double w = constraints.maxWidth - leftMargin;
            if (w <= 0) return;
            
            final double dxLocal = details.localPosition.dx - leftMargin;
            if (dxLocal < 0 || dxLocal > w) return;

            final minX = x.reduce((a, b) => a < b ? a : b);
            final maxX = x.reduce((a, b) => a > b ? a : b);
            final range = maxX - minX;
            if (range == 0) return;

            final double val = minX + (dxLocal / w) * range;
            // Find closest index in x
            int bestIdx = 0;
            double bestDist = (x[0] - val).abs();
            for (int i = 1; i < x.length; i++) {
              double d = (x[i] - val).abs();
              if (d < bestDist) {
                bestDist = d;
                bestIdx = i;
              }
            }
            onTap!(bestIdx);
          },
          child: CustomPaint(
            size: Size(constraints.maxWidth, constraints.maxHeight),
            painter: _LineChartPainter(
              x: x,
              y: y,
              lineColor: lineColor,
              strokeWidth: strokeWidth,
              xLabelBuilder: xLabelBuilder,
              yLabelBuilder: yLabelBuilder,
              partial: partial,
            ),
          ),
        );
      },
    );
  }
}

class _LineChartPainter extends CustomPainter {
  final List<double> x;
  final List<double> y;
  final Color lineColor;
  final double strokeWidth;
  final String Function(double)? xLabelBuilder;
  final String Function(double)? yLabelBuilder;
  final List<bool>? partial;

  _LineChartPainter({
    required this.x,
    required this.y,
    required this.lineColor,
    required this.strokeWidth,
    this.xLabelBuilder,
    this.yLabelBuilder,
    this.partial,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (x.isEmpty || y.isEmpty || x.length != y.length) return;

    final minX = x.reduce((a, b) => a < b ? a : b);
    final maxX = x.reduce((a, b) => a > b ? a : b);
    final minY = y.reduce((a, b) => a < b ? a : b);
    final maxY = y.reduce((a, b) => a > b ? a : b);

    // Margins for axes
    final double bottomMargin = (xLabelBuilder != null) ? 24.0 : 0.0;
    final double leftMargin = (yLabelBuilder != null) ? 48.0 : 0.0;
    
    final double w = size.width - leftMargin;
    final double h = size.height - bottomMargin;

    final dx = (maxX - minX) == 0 ? 1.0 : (maxX - minX);
    final dy = (maxY - minY) == 0 ? 1.0 : (maxY - minY);

    // Draw Axes if margins exist
    if (bottomMargin > 0 || leftMargin > 0) {
      final axisPaint = Paint()
        ..color = Colors.white30
        ..strokeWidth = 1.0;
      
      // Y Axis
      if (leftMargin > 0) {
        canvas.drawLine(Offset(leftMargin, 0), Offset(leftMargin, h), axisPaint);
      }
      // X Axis
      if (bottomMargin > 0) {
        canvas.drawLine(Offset(leftMargin, h), Offset(size.width, h), axisPaint);
      }
    }

    // Draw Y Labels (5 ticks)
    if (yLabelBuilder != null) {
      for (int i = 0; i <= 4; i++) {
        final val = minY + (dy * i / 4);
        final label = yLabelBuilder!(val);
        final tp = TextPainter(
          text: TextSpan(text: label, style: const TextStyle(color: Colors.white70, fontSize: 10)),
          textDirection: TextDirection.ltr,
        );
        tp.layout();
        tp.paint(canvas, Offset(leftMargin - tp.width - 6, h - (i / 4 * h) - tp.height / 2));
      }
    }

    // Draw X Labels (5 ticks)
    if (xLabelBuilder != null) {
      for (int i = 0; i <= 4; i++) {
        final val = minX + (dx * i / 4);
        final label = xLabelBuilder!(val);
        final tp = TextPainter(
          text: TextSpan(text: label, style: const TextStyle(color: Colors.white70, fontSize: 10)),
          textDirection: TextDirection.ltr,
        );
        tp.layout();
        // Shift first and last labels to stay in bounds
        double xOffset = leftMargin + (i / 4 * w) - tp.width / 2;
        if (i == 0) xOffset = leftMargin;
        if (i == 4) xOffset = leftMargin + w - tp.width;
        
        tp.paint(canvas, Offset(xOffset, h + 6));
      }
    }

    final paint = Paint()
      ..color = lineColor
      ..strokeWidth = strokeWidth
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    final dashedPaint = Paint()
      ..color = lineColor.withOpacity(0.5)
      ..strokeWidth = strokeWidth
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    
    // Clip to chart area
    canvas.save();
    canvas.clipRect(Rect.fromLTWH(leftMargin, 0, w, h));

    final path = Path();
    double prevPx = 0;
    double prevPy = 0;

    for (int i = 0; i < x.length; i++) {
      final px = leftMargin + (x[i] - minX) / dx * w;
      final py = h - (y[i] - minY) / dy * h;
      
      if (i == 0) {
        path.moveTo(px, py);
      } else {
        bool isSegPartial = (partial != null && i < partial!.length && partial![i]);
        if (isSegPartial) {
           canvas.drawPath(path, paint);
           path.reset();
           path.moveTo(prevPx, prevPy);
           _drawDashedLine(canvas, Offset(prevPx, prevPy), Offset(px, py), dashedPaint);
           path.moveTo(px, py);
           // Draw a dot for partial
           canvas.drawCircle(Offset(px, py), 3, Paint()..color = Colors.amber);
        } else {
           path.lineTo(px, py);
        }
      }
      prevPx = px;
      prevPy = py;
    }
    canvas.drawPath(path, paint);
    canvas.restore();
  }

  void _drawDashedLine(Canvas canvas, Offset p1, Offset p2, Paint paint) {
    final double dashWidth = 5.0;
    final double dashSpace = 3.0;
    final double distance = (p2 - p1).distance;
    final double dx = (p2.dx - p1.dx) / distance;
    final double dy = (p2.dy - p1.dy) / distance;
    double currentDistance = 0.0;
    while (currentDistance < distance) {
      final double end = currentDistance + dashWidth > distance ? distance : currentDistance + dashWidth;
      canvas.drawLine(
        Offset(p1.dx + dx * currentDistance, p1.dy + dy * currentDistance),
        Offset(p1.dx + dx * end, p1.dy + dy * end),
        paint
      );
      currentDistance += dashWidth + dashSpace;
    }
  }

  @override
  bool shouldRepaint(covariant _LineChartPainter oldDelegate) {
    return oldDelegate.x != x || oldDelegate.y != y || oldDelegate.lineColor != lineColor || oldDelegate.partial != partial;
  }
}

class BarChart extends StatelessWidget {
  final List<double> values;
  final List<double>? overlayValues;
  final List<String>? labels;
  final Color barColor;
  final Color overlayColor;
  final void Function(int)? onTap;

  const BarChart({
    super.key,
    required this.values,
    this.overlayValues,
    this.labels,
    this.barColor = const Color(0xFF818CF8),
    this.overlayColor = const Color(0xFFFF5252),
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      return GestureDetector(
        onTapUp: (details) {
          if (onTap == null || values.isEmpty) return;
          final double barWidth = constraints.maxWidth / values.length;
          final int index = (details.localPosition.dx / barWidth).floor();
          if (index >= 0 && index < values.length) {
            onTap!(index);
          }
        },
        child: CustomPaint(
          size: Size(constraints.maxWidth, constraints.maxHeight),
          painter: _BarChartPainter(values, overlayValues, labels, barColor, overlayColor),
        ),
      );
    });
  }
}

class _BarChartPainter extends CustomPainter {
  final List<double> values;
  final List<double>? overlayValues;
  final List<String>? labels;
  final Color barColor;
  final Color overlayColor;

  _BarChartPainter(this.values, this.overlayValues, this.labels, this.barColor, this.overlayColor);

  @override
  void paint(Canvas canvas, Size size) {
    if (values.isEmpty) return;
    final maxV = values.reduce((a, b) => a > b ? a : b);
    
    // Margins
    final double bottomMargin = (labels != null) ? 24.0 : 0.0;
    final double h = size.height - bottomMargin;
    final double barWidth = size.width / values.length;

    final paint = Paint()
      ..color = barColor
      ..style = PaintingStyle.fill;

    final overlayPaint = Paint()
      ..color = overlayColor
      ..style = PaintingStyle.fill;

    for (int i = 0; i < values.length; i++) {
      final barH = maxV == 0 ? 0 : (values[i] / maxV) * h;
      final rect = Rect.fromLTWH(i * barWidth + 2, h - barH, barWidth - 4, barH.toDouble());
      canvas.drawRRect(RRect.fromRectAndRadius(rect, const Radius.circular(4)), paint);
      
      if (overlayValues != null && i < overlayValues!.length) {
         final ov = overlayValues![i];
         if (ov > 0) {
            final ovH = maxV == 0 ? 0 : (ov / maxV) * h;
            // Draw overlay narrower or just on top?
            // "Overlay anomaly distribution visually"
            // Usually simpler to draw it "inside" the bar or on top.
            // Drawing inside (narrower) is good for visibility.
            final ovRect = Rect.fromLTWH(i * barWidth + 6, h - ovH, barWidth - 12, ovH.toDouble());
            canvas.drawRRect(RRect.fromRectAndRadius(ovRect, const Radius.circular(2)), overlayPaint);
         }
      }

      if (labels != null && i < labels!.length) {
         // ... existing label logic
         bool show = true;
         if (values.length > 10) {
            show = i % (values.length ~/ 5) == 0;
         }

         if (show) {
           final tp = TextPainter(
             text: TextSpan(text: labels![i], style: const TextStyle(color: Colors.white70, fontSize: 10)),
             textDirection: TextDirection.ltr,
             maxLines: 1,
             ellipsis: '...',
           );
           tp.layout(maxWidth: barWidth * 2);
           tp.paint(canvas, Offset(i * barWidth + (barWidth - tp.width)/2, h + 6));
         }
      }
    }
  }

  @override
  bool shouldRepaint(covariant _BarChartPainter oldDelegate) {
    return oldDelegate.values != values || 
           oldDelegate.barColor != barColor || 
           oldDelegate.labels != labels ||
           oldDelegate.overlayValues != overlayValues;
  }
}

class ChartCard extends StatelessWidget {
  final String title;
  final Widget child;
  final double height;
  final bool truncated;
  final String? subtitle;
  final String? pillLabel;
  final String? truncationLabel;
  final String? truncationTooltip;

  const ChartCard({
    super.key,
    required this.title,
    required this.child,
    this.height = 220,
    this.truncated = false,
    this.subtitle,
    this.pillLabel,
    this.truncationLabel,
    this.truncationTooltip,
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
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(title, style: const TextStyle(color: Colors.white70, fontWeight: FontWeight.bold)),
                    if (subtitle != null)
                      Text(subtitle!, style: TextStyle(color: Colors.white.withOpacity(0.4), fontSize: 11)),
                  ],
                ),
              ),
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  if (pillLabel != null) ChartPill(text: pillLabel!),
                  if (truncated)
                    Padding(
                      padding: const EdgeInsets.only(left: 8),
                      child: TruncationBadge(
                        label: truncationLabel ?? 'Truncated',
                        tooltip: truncationTooltip,
                      ),
                    ),
                ],
              ),
            ],
          ),
          const SizedBox(height: 12),
          SizedBox(height: height, child: child),
        ],
      ),
    );
  }
}

class TruncationBadge extends StatelessWidget {
  final String label;
  final String? tooltip;

  const TruncationBadge({
    super.key,
    required this.label,
    this.tooltip,
  });

  @override
  Widget build(BuildContext context) {
    final badge = Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: Colors.orange.withOpacity(0.2),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.orange.withOpacity(0.5)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.warning_amber_rounded, size: 12, color: Colors.orange),
          const SizedBox(width: 4),
          Text(
            label,
            style: TextStyle(
              color: Colors.orange[300],
              fontSize: 10,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
    if (tooltip == null) return badge;
    return Tooltip(message: tooltip!, child: badge);
  }
}

class ChartPill extends StatelessWidget {
  final String text;

  const ChartPill({super.key, required this.text});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: Colors.blueGrey.withOpacity(0.2),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.blueGrey.withOpacity(0.5)),
      ),
      child: Text(
        text,
        style: TextStyle(
          color: Colors.blueGrey[200],
          fontSize: 10,
          fontWeight: FontWeight.bold,
        ),
      ),
    );
  }
}
