import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/widgets/analytics_state_panel.dart';

void main() {
  testWidgets('AnalyticsStatePanel renders loading state', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: AnalyticsStatePanel(
            state: AnalyticsState.loading,
            title: 'Loading',
            message: 'Fetching data',
          ),
        ),
      ),
    );

    expect(find.text('Loading'), findsOneWidget);
    expect(find.text('Fetching data'), findsOneWidget);
    expect(find.text('Retry'), findsNothing);
  });

  testWidgets('AnalyticsStatePanel renders error with retry', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: AnalyticsStatePanel(
            state: AnalyticsState.error,
            title: 'Failed',
            message: 'Request failed',
            onRetry: () {},
          ),
        ),
      ),
    );

    expect(find.text('Failed'), findsOneWidget);
    expect(find.text('Request failed'), findsOneWidget);
    expect(find.text('Retry'), findsOneWidget);
  });

  testWidgets('AnalyticsStatePanel renders empty state', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: AnalyticsStatePanel(
            state: AnalyticsState.empty,
            title: 'Empty',
            message: 'No data available',
          ),
        ),
      ),
    );

    expect(find.text('Empty'), findsOneWidget);
    expect(find.text('No data available'), findsOneWidget);
  });

  testWidgets('AnalyticsStatePanel renders partial state', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: AnalyticsStatePanel(
            state: AnalyticsState.partial,
            title: 'Partial data',
            message: 'Latest bucket incomplete',
          ),
        ),
      ),
    );

    expect(find.text('Partial data'), findsOneWidget);
    expect(find.text('Latest bucket incomplete'), findsOneWidget);
  });
}
