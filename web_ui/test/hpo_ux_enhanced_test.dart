import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/control_panel.dart';
import 'package:web_ui/screens/models_screen.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'dart:convert';

class MockTelemetryService extends TelemetryService {
  MockTelemetryService({super.client});
  
  @override
  Future<List<DatasetRun>> listDatasets({int limit = 50, int offset = 0, String? status, String? createdFrom, String? createdTo}) async => [];
  
  @override
  Future<List<ModelRunSummary>> listModels({int limit = 50, int offset = 0, String? status, String? datasetId, String? createdFrom, String? createdTo}) async => [];
}

void main() {
  group('Enhanced HPO UX Tests', () {
    testWidgets('ControlPanel shows HPO preflight estimation', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1200, 1200);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() => tester.view.resetPhysicalSize());

      final appState = AppState();
      final service = MockTelemetryService();

      await tester.pumpWidget(
        MaterialApp(
          home: MultiProvider(
            providers: [
              ChangeNotifierProvider.value(value: appState),
              Provider<TelemetryService>.value(value: service),
            ],
            child: const Scaffold(body: ControlPanel()),
          ),
        ),
      );

      // Enable HPO
      await tester.tap(find.byType(Switch));
      await tester.pumpAndSettle();

      expect(find.text('Estimated Candidates:'), findsOneWidget);
      expect(find.text('Trials to Run:'), findsOneWidget);
      
      // Default: grid with 2,3,4 and 99.0,99.5,99.9 -> 3*3 = 9 candidates
      expect(find.text('9'), findsNWidgets(2)); 

      // Change N Components space to 2,3
      await tester.enterText(find.byType(TextField).at(5), '2,3'); 
      await tester.pumpAndSettle();

      // Now 2*3 = 6 candidates
      expect(find.text('6'), findsNWidgets(2));
    });

    testWidgets('ControlPanel shows HPO cap warnings', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1200, 1200);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() => tester.view.resetPhysicalSize());

      final appState = AppState();
      final service = MockTelemetryService();

      await tester.pumpWidget(
        MaterialApp(
          home: MultiProvider(
            providers: [
              ChangeNotifierProvider.value(value: appState),
              Provider<TelemetryService>.value(value: service),
            ],
            child: const Scaffold(body: ControlPanel()),
          ),
        ),
      );

      await tester.tap(find.byType(Switch));
      await tester.pumpAndSettle();

      // Enter large grid
      await tester.enterText(find.byType(TextField).at(5), '1,2,3,4,5'); // NComp space is at index 5 in HPO panel
      await tester.enterText(find.byType(TextField).at(6), List.generate(30, (i) => '9${i.toString().padLeft(2, "0")}').join(','));
      await tester.pumpAndSettle();

      expect(find.text('Capped by server limit (max 100 combinations).'), findsOneWidget);
      expect(find.text('100'), findsOneWidget); // Effective trials
    });
   group('Model Detail Metadata Tests', () {
    test('ModelRunSummary parses audit and provenance fields', () {
      final json = {
        'model_run_id': 'm1',
        'dataset_id': 'd1',
        'status': 'COMPLETED',
        'candidate_fingerprint': 'abc123hash',
        'generator_version': 'gen_v1',
        'seed_used': 42,
        'selection_metric_source': 'eval_v1',
        'selection_metric_computed_at': '2026-02-05T12:00:00Z'
      };

      final summary = ModelRunSummary.fromJson(json);
      expect(summary.candidateFingerprint, 'abc123hash');
      expect(summary.generatorVersion, 'gen_v1');
      expect(summary.seedUsed, 42);
    });
  });

  testWidgets('ModelsScreen shows paginated trials and load more', (WidgetTester tester) async {
    tester.view.physicalSize = const Size(1200, 5000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() => tester.view.resetPhysicalSize());

    final appState = AppState();
    final mockService = MockClient((request) async {
      if (request.url.path.endsWith('/models/p1-long-id')) {
        return http.Response(jsonEncode({
          'model_run_id': 'p1-long-id',
          'dataset_id': 'd1-long-id',
          'status': 'RUNNING',
          'hpo_config': {'algorithm': 'grid', 'max_trials': 100},
          'name': 'tuning_run',
          'best_metric_name': 'threshold'
        }), 200);
      }
      if (request.url.path.endsWith('/trials')) {
        final offset = int.parse(request.url.queryParameters['offset'] ?? '0');
        final items = List.generate(50, (i) => {
          'model_run_id': 't${offset + i}-long-id',
          'trial_index': offset + i,
          'status': 'COMPLETED',
          'trial_params': {'n': i},
          'is_eligible': true,
          'selection_metric_value': 0.5,
          'name': 't${offset + i}',
          'dataset_id': 'd1-long-id'
        });
        return http.Response(jsonEncode({
          'items': items,
          'limit': 50,
          'offset': offset
        }), 200);
      }
      if (request.url.path.endsWith('/models')) {
        return http.Response(jsonEncode({
          'items': [
            {
              'model_run_id': 'p1-long-id',
              'dataset_id': 'd1-long-id',
              'name': 'tuning_run',
              'status': 'RUNNING',
              'created_at': '2026-02-05T00:00:00Z',
              'hpo_summary': {'trial_count': 100, 'completed_count': 50}
            }
          ],
          'limit': 50,
          'offset': 0
        }), 200);
      }
      if (request.url.path.endsWith('/scored')) {
        return http.Response(jsonEncode([]), 200);
      }
      return http.Response('Not Found', 404);
    });

    final service = TelemetryService(client: mockService);

    await tester.pumpWidget(
      MaterialApp(
        home: MultiProvider(
          providers: [
            ChangeNotifierProvider.value(value: appState),
            Provider<TelemetryService>.value(value: service),
          ],
          child: const Scaffold(body: ModelsScreen()),
        ),
      ),
    );

    await tester.pumpAndSettle();

    expect(find.text('tuning_run'), findsOneWidget);
    await tester.tap(find.text('tuning_run'));
    await tester.pumpAndSettle();
    
    // Wait for the async _fetchTrialsPage to complete
    for (int i = 0; i < 5; i++) {
      await tester.pump(const Duration(milliseconds: 100));
    }

    expect(find.text('Trials'), findsOneWidget);
    
    // The detail view is the second ListView
    final detailList = find.byType(ListView).at(1);
    await tester.drag(detailList, const Offset(0, -3000));
    await tester.pumpAndSettle();

    final loadMoreFinder = find.text('Load More Trials');
    expect(loadMoreFinder, findsOneWidget);

    await tester.tap(loadMoreFinder);
    await tester.pumpAndSettle();
    
    for (int i = 0; i < 5; i++) {
      await tester.pump(const Duration(milliseconds: 100));
    }

    // After load more, we should have 100 trials (0-99)
    // Just check for some from the second page
    expect(find.text('50'), findsWidgets);
    expect(find.text('99'), findsWidgets);
  });
  });
}
