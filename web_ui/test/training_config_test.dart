import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'dart:convert';

void main() {
  group('TelemetryService Training Config', () {
    test('trainModel passes nComponents and percentile correctly', () async {
      String? capturedBody;
      final mockClient = MockClient((request) async {
        capturedBody = request.body;
        return http.Response(
          jsonEncode({'model_run_id': 'run-123'}),
          202,
        );
      });

      final service = TelemetryService(client: mockClient);
      await service.trainModel('ds-1', nComponents: 4, percentile: 98.5);

      expect(capturedBody, isNotNull);
      final body = jsonDecode(capturedBody!);
      expect(body['dataset_id'], 'ds-1');
      expect(body['n_components'], 4);
      expect(body['percentile'], 98.5);
    });

    test('trainModel uses defaults when parameters omitted', () async {
      String? capturedBody;
      final mockClient = MockClient((request) async {
        capturedBody = request.body;
        return http.Response(
          jsonEncode({'model_run_id': 'run-123'}),
          202,
        );
      });

      final service = TelemetryService(client: mockClient);
      await service.trainModel('ds-1');

      expect(capturedBody, isNotNull);
      final body = jsonDecode(capturedBody!);
      expect(body['dataset_id'], 'ds-1');
      expect(body.containsKey('n_components'), isFalse);
      expect(body.containsKey('percentile'), isFalse);
    });
  });
}
