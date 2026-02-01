import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:web_ui/screens/control_panel.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'package:web_ui/state/app_state.dart';

class FakeTelemetryService extends TelemetryService {
  FakeTelemetryService() : super(baseUrl: 'http://example.com');

  @override
  Future<List<DatasetRun>> listDatasets(
      {int limit = 50,
      int offset = 0,
      String? status,
      String? createdFrom,
      String? createdTo}) async {
    return [
      DatasetRun(
        runId: 'ds-1',
        status: 'COMPLETED',
        insertedRows: 10,
        createdAt: 'now',
        startTime: 'now',
        endTime: 'now',
        intervalSeconds: 60,
        hostCount: 1,
        tier: 'test',
      ),
    ];
  }

  @override
  Future<List<ModelRunSummary>> listModels(
      {int limit = 50,
      int offset = 0,
      String? status,
      String? datasetId,
      String? createdFrom,
      String? createdTo}) async {
    return [
      ModelRunSummary(
        modelRunId: 'model-1',
        datasetId: 'ds-1',
        name: 'model',
        status: 'COMPLETED',
        artifactPath: '/tmp/model.json',
        createdAt: 'now',
        completedAt: 'now',
      ),
    ];
  }

  @override
  Future<DatasetStatus> getDatasetStatus(String id) async {
    return DatasetStatus(runId: id, status: 'COMPLETED', rowsInserted: 10);
  }

  @override
  Future<ModelStatus> getModelStatus(String id) async {
    return ModelStatus(
      modelRunId: id,
      datasetId: 'ds-1',
      name: 'model',
      status: 'COMPLETED',
    );
  }

  @override
  Future<List<Map<String, dynamic>>> getDatasetSamples(String runId, {int limit = 20}) async {
    return [];
  }

  @override
  Future<Map<String, dynamic>> getDatasetRecord(String runId, int recordId) async {
    return {
      'cpu_usage': 10.0,
      'memory_usage': 20.0,
      'disk_utilization': 30.0,
      'network_rx_rate': 1.0,
      'network_tx_rate': 2.0,
      'timestamp': '2024-01-01T00:00:00Z',
      'host_id': 'host-1',
    };
  }

  @override
  Future<InferenceResponse> runInference(
      String modelId, List<Map<String, dynamic>> samples) async {
    return InferenceResponse(
      results: [InferenceResult(isAnomaly: false, score: 0.1234)],
      modelRunId: modelId,
      inferenceId: 'inf-1',
      anomalyCount: 0,
    );
  }
}

void main() {
  testWidgets('inference handoff hydrates pending record and runs inference',
      (WidgetTester tester) async {
    final appState = AppState();
    appState.setDataset('ds-1');
    appState.setModel('model-1');
    appState.setPendingInference(
      PendingInferenceRequest(
        datasetId: 'ds-1',
        modelId: 'model-1',
        recordId: '1',
      ),
    );

    await tester.pumpWidget(
      MultiProvider(
        providers: [
          Provider<TelemetryService>(create: (_) => FakeTelemetryService()),
          ChangeNotifierProvider<AppState>.value(value: appState),
        ],
        child: const MaterialApp(
          home: Scaffold(
            body: ControlPanel(),
          ),
        ),
      ),
    );

    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    expect(find.textContaining('Loaded record 1 from results.'), findsOneWidget);
    expect(find.text('Results'), findsOneWidget);
  });
}
