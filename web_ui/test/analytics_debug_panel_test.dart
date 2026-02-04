import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/widgets/analytics_debug_panel.dart';

void main() {
  testWidgets('AnalyticsDebugPanel shows request metadata', (tester) async {
    await tester.pumpWidget(
      ChangeNotifierProvider(
        create: (_) => AppState(),
        child: const MaterialApp(
          home: Scaffold(
            body: AnalyticsDebugPanel(
              requestId: 'req-123',
              paramsSummary: 'endpoint=topk, column=region',
              durationMs: 12.3,
              cacheHit: false,
              serverTime: '2026-02-03T00:00:00Z',
            ),
          ),
        ),
      ),
    );

    expect(find.text('Debug'), findsOneWidget);
    await tester.tap(find.text('Debug'));
    await tester.pumpAndSettle();

    expect(find.text('Request ID'), findsOneWidget);
    expect(find.text('req-123'), findsOneWidget);
    expect(find.textContaining('endpoint=topk'), findsOneWidget);
    expect(find.textContaining('12.3 ms'), findsOneWidget);
  });
}
