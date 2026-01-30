import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:web_ui/services/telemetry_service.dart';
import 'dart:convert';

void main() {
  group('TelemetryService Error Handling', () {
    test('Parses structured error envelope', () async {
      final mockClient = MockClient((request) async {
        return http.Response(
          jsonEncode({
            'error': {
              'message': 'Job queue full',
              'code': 'RESOURCE_EXHAUSTED',
              'request_id': 'req-123'
            }
          }),
          503,
        );
      });

      final service = TelemetryService(client: mockClient);

      try {
        await service.generateDataset(10);
        fail('Should have thrown');
      } catch (e) {
        expect(e.toString(), contains('Job queue full'));
        expect(e.toString(), contains('Code: RESOURCE_EXHAUSTED'));
        expect(e.toString(), contains('RequestID: req-123'));
      }
    });

    test('Parses partial structured error', () async {
      final mockClient = MockClient((request) async {
        return http.Response(
          jsonEncode({
            'error': {
              'message': 'Something went wrong',
            }
          }),
          400,
        );
      });

      final service = TelemetryService(client: mockClient);

      try {
        await service.generateDataset(10);
        fail('Should have thrown');
      } catch (e) {
        expect(e.toString(), contains('Something went wrong'));
        expect(e.toString(), contains('Code: UNKNOWN'));
      }
    });

    test('Handles non-JSON error gracefully', () async {
      final mockClient = MockClient((request) async {
        return http.Response('Bad Gateway', 502);
      });

      final service = TelemetryService(client: mockClient);

      try {
        await service.generateDataset(10);
        fail('Should have thrown');
      } catch (e) {
        expect(e.toString(), contains('Failed to generate dataset: Bad Gateway'));
      }
    });
  });
}
