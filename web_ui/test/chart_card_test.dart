import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/widgets/charts.dart';

void main() {
  testWidgets('ChartCard shows pill and truncation badge', (WidgetTester tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: ChartCard(
            title: 'Top Regions',
            pillLabel: 'Top 10',
            truncated: true,
            truncationLabel: 'Truncated',
            child: SizedBox(),
          ),
        ),
      ),
    );

    expect(find.text('Top 10'), findsOneWidget);
    expect(find.text('Truncated'), findsOneWidget);
  });

  testWidgets('ChartCard shows bins capped note', (WidgetTester tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: ChartCard(
            title: 'Histogram',
            subtitle: 'Bins capped at 500',
            truncated: true,
            truncationLabel: 'Bins capped',
            child: SizedBox(),
          ),
        ),
      ),
    );

    expect(find.text('Bins capped at 500'), findsOneWidget);
    expect(find.text('Bins capped'), findsOneWidget);
  });

  testWidgets('ChartCard shows info icon when infoText provided', (WidgetTester tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: ChartCard(
            title: 'Cost Info',
            infoText: 'Duration 12ms',
            child: SizedBox(),
          ),
        ),
      ),
    );

    expect(find.byIcon(Icons.info_outline), findsOneWidget);
  });
}
