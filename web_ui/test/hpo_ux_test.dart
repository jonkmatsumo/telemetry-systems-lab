import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'dart:convert';

void main() {
  group('HPO Logic and Schema', () {
    test('trainModel includes hpo_config when provided', () async {
      String? capturedBody;
      final mockClient = MockClient((request) async {
        capturedBody = request.body;
        return http.Response(jsonEncode({'model_run_id': 'hpo-123'}), 202);
      });

      final service = TelemetryService(client: mockClient);
      final hpoConfig = {
        'algorithm': 'grid',
        'max_trials': 20,
        'search_space': {
          'n_components': [2, 3],
          'percentile': [99.0, 99.5]
        }
      };

      await service.trainModel('ds-1', hpoConfig: hpoConfig);

      expect(capturedBody, isNotNull);
      final body = jsonDecode(capturedBody!);
      expect(body['hpo_config'], hpoConfig);
    });

    test('ModelRunSummary handles missing HPO fields gracefully', () {
      final json = {
        'model_run_id': 'm1',
        'dataset_id': 'd1',
        'name': 'test',
        'status': 'COMPLETED',
        'created_at': '2026-02-04T00:00:00Z',
      };

      final summary = ModelRunSummary.fromJson(json);
      expect(summary.parentRunId, isNull);
      expect(summary.trialIndex, isNull);
      expect(summary.hpoSummary, isNull);
      expect(summary.isEligible, isTrue); // Default
      expect(summary.selectionMetricValue, isNull);
    });

    test('ModelRunSummary parses HPO summary and selection info', () {
      final json = {
        'model_run_id': 'm1',
        'dataset_id': 'd1',
        'status': 'COMPLETED',
        'parent_run_id': 'p1',
        'trial_index': 5,
        'is_eligible': false,
        'eligibility_reason': 'FAILED',
        'selection_metric_value': 0.123,
        'hpo_summary': {
          'trial_count': 10,
          'completed_count': 8,
          'best_metric_value': 0.123
        }
      };

      final summary = ModelRunSummary.fromJson(json);
      expect(summary.parentRunId, 'p1');
      expect(summary.trialIndex, 5);
      expect(summary.isEligible, isFalse);
      expect(summary.eligibilityReason, 'FAILED');
      expect(summary.selectionMetricValue, 0.123);
      expect(summary.hpoSummary?['trial_count'], 10);
    });
  });
}
