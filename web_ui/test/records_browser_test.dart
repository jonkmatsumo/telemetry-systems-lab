import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/dataset_analytics_screen.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/state/investigation_context.dart';

class _CapturingTelemetryService extends TelemetryService {
  _CapturingTelemetryService() : super(baseUrl: 'http://example.com');

  String? lastSortBy;
  String? lastSortOrder;
  String? lastAnchorTime;

  @override
  Future<Map<String, dynamic>> searchDatasetRecords(String datasetId,
      {int limit = 20,
      int offset = 0,
      String? sortBy,
      String? sortOrder,
      String? anchorTime,
      String? region,
      String? anomalyType,
      String? isAnomaly,
      String? startTime,
      String? endTime}) async {
    lastSortBy = sortBy;
    lastSortOrder = sortOrder;
    lastAnchorTime = anchorTime;
    return {'items': <Map<String, dynamic>>[], 'total': 0, 'has_more': false};
  }
}

void main() {
  testWidgets('RecordsBrowser shows back button and context chips', (tester) async {
    final service = _CapturingTelemetryService();
    final ctx = InvestigationContext(
      datasetId: 'ds-1',
      metric: 'cpu_usage',
      useUtc: true,
      region: 'us-east',
      anomalyType: 'spike',
      isAnomaly: 'true',
      startTime: '2026-02-03T00:00:00Z',
      endTime: '2026-02-03T01:00:00Z',
    );

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => service),
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: MaterialApp(
          home: Scaffold(
            body: RecordsBrowser(
              datasetId: 'ds-1',
              metric: 'cpu_usage',
              useUtc: true,
              contextSeed: ctx,
            ),
          ),
        ),
      ),
    );

    await tester.pump();
    expect(find.text('Back to aggregate'), findsOneWidget);
    expect(find.text('TZ: UTC'), findsOneWidget);
    expect(find.text('Region: us-east'), findsOneWidget);
  });

  testWidgets('RecordsBrowser forwards sort and anchor params', (tester) async {
    final service = _CapturingTelemetryService();
    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => service),
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: RecordsBrowser(
              datasetId: 'ds-1',
              metric: 'cpu_usage',
              useUtc: true,
            ),
          ),
        ),
      ),
    );

    await tester.pump();
    expect(service.lastSortOrder, 'desc');

    await tester.tap(find.byKey(const Key('records-sort-order')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Oldest first').last);
    await tester.pump();
    expect(service.lastSortOrder, 'asc');

    await tester.enterText(find.byKey(const Key('records-jump-input')), '2026-02-03T00:00:00Z');
    await tester.tap(find.byKey(const Key('records-jump-apply')));
    await tester.pump();
    expect(service.lastAnchorTime, '2026-02-03T00:00:00Z');
  });

  testWidgets('RecordsBrowser disables next when has_more is false', (tester) async {
    final service = _CapturingTelemetryService();
    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => service),
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: RecordsBrowser(
              datasetId: 'ds-1',
              metric: 'cpu_usage',
              useUtc: true,
            ),
          ),
        ),
      ),
    );

    await tester.pump();
    final nextButton = find.byIcon(Icons.chevron_right);
    final iconButton = tester.widget<IconButton>(nextButton);
    expect(iconButton.onPressed, isNull);
  });
}
