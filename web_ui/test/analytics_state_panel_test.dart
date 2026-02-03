import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/widgets/analytics_state_panel.dart';

void main() {
  testWidgets('AnalyticsStatePanel renders loading state', (WidgetTester tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(
        body: AnalyticsStatePanel(
          state: AnalyticsState.loading,
          title: 'Loading...',
          message: 'Please wait',
        ),
      ),
    ));

    expect(find.text('Loading...'), findsOneWidget);
    expect(find.byIcon(Icons.hourglass_top), findsOneWidget);
  });

  testWidgets('AnalyticsStatePanel renders error state with retry', (WidgetTester tester) async {
    bool retried = false;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: AnalyticsStatePanel(
          state: AnalyticsState.error,
          title: 'Error',
          message: 'Something went wrong',
          onRetry: () => retried = true,
        ),
      ),
    ));

    expect(find.byIcon(Icons.error_outline), findsOneWidget);
    await tester.tap(find.text('Retry'));
    expect(retried, true);
  });

  testWidgets('AnalyticsStatePanel renders partial state', (WidgetTester tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: Scaffold(
        body: AnalyticsStatePanel(
          state: AnalyticsState.partial,
          title: 'Partial Data',
          message: 'Incomplete bucket',
        ),
      ),
    ));

    expect(find.byIcon(Icons.warning_amber_rounded), findsOneWidget);
    // Color check is hard in widget tests without inspecting render object properties directly
    // but icon presence confirms the switch case.
  });
}