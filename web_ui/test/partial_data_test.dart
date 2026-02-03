import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/widgets/analytics_state_panel.dart';
import 'package:web_ui/widgets/charts.dart';

// Mock chart that accepts partial data signal
class MockLineChart extends StatelessWidget {
  final List<bool> partial;
  const MockLineChart({super.key, required this.partial});
  @override
  Widget build(BuildContext context) {
    if (partial.any((p) => p)) {
      return Column(
        children: [
          const AnalyticsStatePanel(
            state: AnalyticsState.partial,
            title: 'Partial data',
            message: 'Partial data detected',
          ),
          Container(height: 100, color: Colors.blue),
        ],
      );
    }
    return Container(height: 100, color: Colors.blue);
  }
}

void main() {
  testWidgets('Displays partial data warning when bucket counts drop', (WidgetTester tester) async {
    // Simulate data where the last bucket has significantly fewer counts
    final counts = [100, 100, 100, 10]; // Last one is 10%
    final maxCount = counts.reduce((a, b) => a > b ? a : b);
    final partial = counts.map((c) => c < maxCount * 0.9).toList();

    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: MockLineChart(partial: partial),
      ),
    ));

    expect(find.byType(AnalyticsStatePanel), findsOneWidget);
    expect(find.text('Partial data'), findsOneWidget);
    expect(find.byIcon(Icons.warning_amber_rounded), findsOneWidget);
  });

  testWidgets('Does not display warning when counts are stable', (WidgetTester tester) async {
    final counts = [100, 95, 105, 98];
    final maxCount = counts.reduce((a, b) => a > b ? a : b);
    final partial = counts.map((c) => c < maxCount * 0.9).toList();

    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: MockLineChart(partial: partial),
      ),
    ));

    expect(find.byType(AnalyticsStatePanel), findsNothing);
  });
}
