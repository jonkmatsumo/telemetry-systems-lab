import 'package:flutter/material.dart';

class LineChart extends StatefulWidget {
  final List<double> x;
  final List<double> y;
  final List<double>? overlayY;
  final Color lineColor;
  final Color overlayColor;
  final double strokeWidth;
  final double overlayStrokeWidth;
  final String Function(double)? xLabelBuilder;
  final String Function(double)? yLabelBuilder;
  final List<bool>? partial;
  final void Function(int)? onTap;

  const LineChart({
    super.key,
    required this.x,
    required this.y,
    this.overlayY,
    this.lineColor = const Color(0xFF38BDF8),
    this.overlayColor = const Color(0xFF94A3B8),
    this.strokeWidth = 2.0,
    this.overlayStrokeWidth = 2.0,
    this.xLabelBuilder,
    this.yLabelBuilder,
    this.partial,
    this.onTap,
  });

  @override
  State<LineChart> createState() => _LineChartState();
}

class _LineChartState extends State<LineChart> {
  int? _hoverIndex;
  Offset? _hoverPosition;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final leftMargin = (widget.yLabelBuilder != null) ? 48.0 : 0.0;
        final bottomMargin = (widget.xLabelBuilder != null) ? 24.0 : 0.0;
        final width = constraints.maxWidth;
        final height = constraints.maxHeight;
        final chartWidth = width - leftMargin;
        final chartHeight = height - bottomMargin;

        return MouseRegion(
          onHover: (event) {
            final idx = _nearestIndexForX(event.localPosition, chartWidth, leftMargin);
            if (idx == null) return;
            setState(() {
              _hoverIndex = idx;
              _hoverPosition = event.localPosition;
            });
          },
          onExit: (_) {
            setState(() {
              _hoverIndex = null;
              _hoverPosition = null;
            });
          },
          child: GestureDetector(
            onTapUp: (details) {
              if (widget.onTap == null || widget.x.isEmpty) return;
              final idx = _nearestIndexForX(details.localPosition, chartWidth, leftMargin);
              if (idx != null) {
                widget.onTap!(idx);
              }
            },
            child: Stack(
              children: [
                CustomPaint(
                  size: Size(width, height),
                  painter: _LineChartPainter(
                    x: widget.x,
                    y: widget.y,
                    overlayY: widget.overlayY,
                    lineColor: widget.lineColor,
                    overlayColor: widget.overlayColor,
                    strokeWidth: widget.strokeWidth,
                    overlayStrokeWidth: widget.overlayStrokeWidth,
                    xLabelBuilder: widget.xLabelBuilder,
                    yLabelBuilder: widget.yLabelBuilder,
                    partial: widget.partial,
                    hoverIndex: _hoverIndex,
                  ),
                ),
                if (_hoverIndex != null &&
                    _hoverPosition != null &&
                    _hoverIndex! < widget.x.length &&
                    _hoverIndex! < widget.y.length &&
                    chartWidth > 0 &&
                    chartHeight > 0)
                  _ChartHoverTooltip(
                    position: _hoverPosition!,
                    bounds: Size(width, height),
                    leftMargin: leftMargin,
                    bottomMargin: bottomMargin,
                  xLabel: _formatValue(widget.x[_hoverIndex!], widget.xLabelBuilder),
                  yLabel: _formatValue(widget.y[_hoverIndex!], widget.yLabelBuilder),
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  int? _nearestIndexForX(Offset localPosition, double chartWidth, double leftMargin) {
    if (widget.x.isEmpty || chartWidth <= 0) return null;
    final dxLocal = localPosition.dx - leftMargin;
    if (dxLocal < 0 || dxLocal > chartWidth) return null;

    final minX = widget.x.reduce((a, b) => a < b ? a : b);
    final maxX = widget.x.reduce((a, b) => a > b ? a : b);
    final range = maxX - minX;
    if (range == 0) return null;

    final val = minX + (dxLocal / chartWidth) * range;
    int bestIdx = 0;
    double bestDist = (widget.x[0] - val).abs();
    for (int i = 1; i < widget.x.length; i++) {
      final d = (widget.x[i] - val).abs();
      if (d < bestDist) {
        bestDist = d;
        bestIdx = i;
      }
    }
    return bestIdx;
  }
}

class _LineChartPainter extends CustomPainter {
  final List<double> x;
  final List<double> y;
  final List<double>? overlayY;
  final Color lineColor;
  final Color overlayColor;
  final double strokeWidth;
  final double overlayStrokeWidth;
  final String Function(double)? xLabelBuilder;
  final String Function(double)? yLabelBuilder;
  final List<bool>? partial;
  final int? hoverIndex;

  _LineChartPainter({
    required this.x,
    required this.y,
    this.overlayY,
    required this.lineColor,
    required this.overlayColor,
    required this.strokeWidth,
    required this.overlayStrokeWidth,
    this.xLabelBuilder,
    this.yLabelBuilder,
    this.partial,
    this.hoverIndex,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (x.isEmpty || y.isEmpty || x.length != y.length) return;

    final minX = x.reduce((a, b) => a < b ? a : b);
    final maxX = x.reduce((a, b) => a > b ? a : b);
    double minY = y.reduce((a, b) => a < b ? a : b);
    double maxY = y.reduce((a, b) => a > b ? a : b);
    if (overlayY != null && overlayY!.isNotEmpty) {
      final minOverlay = overlayY!.reduce((a, b) => a < b ? a : b);
      final maxOverlay = overlayY!.reduce((a, b) => a > b ? a : b);
      if (minOverlay < minY) minY = minOverlay;
      if (maxOverlay > maxY) maxY = maxOverlay;
    }

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
      ..color = lineColor.withValues(alpha: 0.5)
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

    if (overlayY != null && overlayY!.isNotEmpty && overlayY!.length == x.length) {
      final overlayPaint = Paint()
        ..color = overlayColor
        ..strokeWidth = overlayStrokeWidth
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round;
      final overlayPath = Path();
      for (int i = 0; i < x.length; i++) {
        final px = leftMargin + (x[i] - minX) / dx * w;
        final py = h - (overlayY![i] - minY) / dy * h;
        if (i == 0) {
          overlayPath.moveTo(px, py);
        } else {
          overlayPath.lineTo(px, py);
        }
      }
      canvas.drawPath(overlayPath, overlayPaint);
    }

    if (hoverIndex != null && hoverIndex! >= 0 && hoverIndex! < x.length) {
      final idx = hoverIndex!;
      final hx = leftMargin + (x[idx] - minX) / dx * w;
      final hy = h - (y[idx] - minY) / dy * h;
      canvas.drawCircle(
        Offset(hx, hy),
        4,
        Paint()..color = Colors.white.withValues(alpha: 0.9),
      );
      canvas.drawCircle(
        Offset(hx, hy),
        2,
        Paint()..color = lineColor,
      );
    }
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
    return oldDelegate.x != x ||
        oldDelegate.y != y ||
        oldDelegate.overlayY != overlayY ||
        oldDelegate.lineColor != lineColor ||
        oldDelegate.overlayColor != overlayColor ||
        oldDelegate.partial != partial ||
        oldDelegate.hoverIndex != hoverIndex;
  }
}

class BarChart extends StatefulWidget {
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
  State<BarChart> createState() => _BarChartState();
}

class _BarChartState extends State<BarChart> {
  int? _hoverIndex;
  Offset? _hoverPosition;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      final width = constraints.maxWidth;
      final height = constraints.maxHeight;
      return MouseRegion(
        onHover: (event) {
          final idx = _barIndexForX(event.localPosition.dx, width);
          if (idx == null) return;
          setState(() {
            _hoverIndex = idx;
            _hoverPosition = event.localPosition;
          });
        },
        onExit: (_) {
          setState(() {
            _hoverIndex = null;
            _hoverPosition = null;
          });
        },
        child: GestureDetector(
          onTapUp: (details) {
            if (widget.onTap == null || widget.values.isEmpty) return;
            final idx = _barIndexForX(details.localPosition.dx, width);
            if (idx != null) {
              widget.onTap!(idx);
            }
          },
          child: Stack(
            children: [
              CustomPaint(
                size: Size(width, height),
                painter: _BarChartPainter(
                  widget.values,
                  widget.overlayValues,
                  widget.labels,
                  widget.barColor,
                  widget.overlayColor,
                  hoverIndex: _hoverIndex,
                ),
              ),
              if (_hoverIndex != null && _hoverPosition != null)
                _ChartHoverTooltip(
                  position: _hoverPosition!,
                  bounds: Size(width, height),
                  leftMargin: 0.0,
                  bottomMargin: widget.labels != null ? 24.0 : 0.0,
                  xLabel: widget.labels != null && _hoverIndex! < widget.labels!.length
                      ? widget.labels![_hoverIndex!]
                      : _hoverIndex!.toString(),
                  yLabel: _formatValue(widget.values[_hoverIndex!], null),
                ),
            ],
          ),
        ),
      );
    });
  }

  int? _barIndexForX(double dx, double width) {
    if (widget.values.isEmpty || width <= 0) return null;
    final barWidth = width / widget.values.length;
    final index = (dx / barWidth).floor();
    if (index < 0 || index >= widget.values.length) return null;
    return index;
  }
}

class _BarChartPainter extends CustomPainter {
  final List<double> values;
  final List<double>? overlayValues;
  final List<String>? labels;
  final Color barColor;
  final Color overlayColor;
  final int? hoverIndex;

  _BarChartPainter(
    this.values,
    this.overlayValues,
    this.labels,
    this.barColor,
    this.overlayColor, {
    this.hoverIndex,
  });

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

      if (hoverIndex == i) {
        final highlightPaint = Paint()
          ..color = Colors.white.withValues(alpha: 0.6)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5;
        canvas.drawRRect(RRect.fromRectAndRadius(rect, const Radius.circular(4)), highlightPaint);
      }
      
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
           oldDelegate.overlayValues != overlayValues ||
           oldDelegate.hoverIndex != hoverIndex;
  }
}

String _formatValue(double value, String Function(double)? formatter) {
  if (!value.isFinite) return 'N/A';
  if (formatter != null) {
    return formatter(value);
  }
  return value.toStringAsFixed(2);
}

class _ChartHoverTooltip extends StatelessWidget {
  final Offset position;
  final Size bounds;
  final double leftMargin;
  final double bottomMargin;
  final String xLabel;
  final String yLabel;

  const _ChartHoverTooltip({
    required this.position,
    required this.bounds,
    required this.leftMargin,
    required this.bottomMargin,
    required this.xLabel,
    required this.yLabel,
  });

  @override
  Widget build(BuildContext context) {
    const double tooltipWidth = 160;
    const double tooltipHeight = 44;
    final maxX = bounds.width - tooltipWidth - 8;
    final maxY = bounds.height - bottomMargin - tooltipHeight - 8;
    final clampedX = position.dx.clamp(leftMargin + 4, maxX).toDouble();
    final clampedY = position.dy.clamp(4, maxY).toDouble();

    return Positioned(
      left: clampedX,
      top: clampedY,
      child: Material(
        color: Colors.transparent,
        child: Container(
          width: tooltipWidth,
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xFF0B1220),
            borderRadius: BorderRadius.circular(6),
            border: Border.all(color: Colors.white12),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.35),
                blurRadius: 8,
                offset: const Offset(0, 4),
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text('x: $xLabel', style: const TextStyle(color: Colors.white70, fontSize: 11)),
              Text('y: $yLabel', style: const TextStyle(color: Colors.white70, fontSize: 11)),
            ],
          ),
        ),
      ),
    );
  }
}

class ChartCard extends StatelessWidget {
  final String title;
  final Widget child;
  final double height;
  final bool truncated;
  final String? subtitle;
  final String? pillLabel;
  final List<String>? pillLabels;
  final String? truncationLabel;
  final String? truncationTooltip;
  final String? footerText;
  final String? infoText;
  final Widget? debugPanel;

  const ChartCard({
    super.key,
    required this.title,
    required this.child,
    this.height = 220,
    this.truncated = false,
    this.subtitle,
    this.pillLabel,
    this.pillLabels,
    this.truncationLabel,
    this.truncationTooltip,
    this.footerText,
    this.infoText,
    this.debugPanel,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.black.withValues(alpha: 0.2),
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
                      Text(subtitle!, style: TextStyle(color: Colors.white.withValues(alpha: 0.4), fontSize: 11)),
                  ],
                ),
              ),
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  if (infoText != null)
                    Tooltip(
                      message: infoText!,
                      child: const Padding(
                        padding: EdgeInsets.only(right: 6),
                        child: Icon(Icons.info_outline, size: 14, color: Colors.white38),
                      ),
                    ),
                  if (pillLabels != null)
                    for (int i = 0; i < pillLabels!.length; i++) ...[
                      ChartPill(text: pillLabels![i]),
                      if (i < pillLabels!.length - 1) const SizedBox(width: 6),
                    ]
                  else if (pillLabel != null)
                    ChartPill(text: pillLabel!),
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
          if (debugPanel != null) ...[
            const SizedBox(height: 8),
            debugPanel!,
          ],
          if (footerText != null) ...[
            const SizedBox(height: 8),
            Text(
              footerText!,
              style: TextStyle(color: Colors.white.withValues(alpha: 0.5), fontSize: 11),
            ),
          ],
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
        color: Colors.orange.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.orange.withValues(alpha: 0.5)),
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
        color: Colors.blueGrey.withValues(alpha: 0.2),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: Colors.blueGrey.withValues(alpha: 0.5)),
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
