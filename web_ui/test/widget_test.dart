import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/control_panel.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';

class _SmokeTelemetryService extends TelemetryService {
  _SmokeTelemetryService() : super(baseUrl: 'http://example.com');

  @override
  Future<List<DatasetRun>> listDatasets(
      {int limit = 50,
      int offset = 0,
      String? status,
      String? createdFrom,
      String? createdTo}) async {
    return [];
  }

  @override
  Future<List<ModelRunSummary>> listModels(
      {int limit = 50,
      int offset = 0,
      String? status,
      String? datasetId,
      String? createdFrom,
      String? createdTo}) async {
    return [];
  }
}

void main() {
  testWidgets('ControlPanel smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => _SmokeTelemetryService()),
          ChangeNotifierProvider(create: (_) => AppState()),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: ControlPanel(),
          ),
        ),
      ),
    );

    await tester.pump();
    expect(find.byType(ControlPanel), findsOneWidget);
  });
}
