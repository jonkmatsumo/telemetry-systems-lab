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
              'request_id': 'req-123',
            },
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
            'error': {'message': 'Something went wrong'},
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
        expect(
          e.toString(),
          contains('Failed to generate dataset: Bad Gateway'),
        );
      }
    });
  });

  group('TelemetryService Timeout Handling', () {
    test('Applies timeout to GET requests with consistent message', () async {
      final mockClient = MockClient((request) async {
        await Future<void>.delayed(const Duration(milliseconds: 80));
        return http.Response(jsonEncode({'metrics': []}), 200);
      });

      final service = TelemetryService(
        client: mockClient,
        requestTimeout: const Duration(milliseconds: 20),
        maxRetries: 0,
      );

      await expectLater(
        service.getMetricsSchema(),
        throwsA(
          predicate(
            (e) =>
                e.toString().contains('Request timed out') &&
                e.toString().contains('GET /schema/metrics'),
          ),
        ),
      );
    });

    test('Applies timeout to POST requests with consistent message', () async {
      final mockClient = MockClient((request) async {
        await Future<void>.delayed(const Duration(milliseconds: 80));
        return http.Response(jsonEncode({'run_id': 'ds-1'}), 202);
      });

      final service = TelemetryService(
        client: mockClient,
        requestTimeout: const Duration(milliseconds: 20),
        maxRetries: 0,
      );

      await expectLater(
        service.generateDataset(10),
        throwsA(
          predicate(
            (e) =>
                e.toString().contains('Request timed out') &&
                e.toString().contains('POST /datasets'),
          ),
        ),
      );
    });
  });

  group('TelemetryService Retry Handling', () {
    test('Retries transient GET failures with capped backoff', () async {
      var attempts = 0;
      final mockClient = MockClient((request) async {
        attempts++;
        if (attempts < 3) {
          return http.Response('temporary outage', 503);
        }
        return http.Response(jsonEncode({'metrics': []}), 200);
      });

      final service = TelemetryService(
        client: mockClient,
        maxRetries: 3,
        initialRetryDelay: const Duration(milliseconds: 1),
        maxRetryDelay: const Duration(milliseconds: 1),
      );

      final metrics = await service.getMetricsSchema();
      expect(metrics, isEmpty);
      expect(attempts, 3);
    });

    test('Stops retrying after max retries for persistent 5xx', () async {
      var attempts = 0;
      final mockClient = MockClient((request) async {
        attempts++;
        return http.Response('temporary outage', 503);
      });

      final service = TelemetryService(
        client: mockClient,
        maxRetries: 2,
        initialRetryDelay: const Duration(milliseconds: 1),
        maxRetryDelay: const Duration(milliseconds: 1),
      );

      await expectLater(
        service.getMetricsSchema(),
        throwsA(
          predicate(
            (e) => e.toString().contains('Failed to get metrics schema'),
          ),
        ),
      );
      expect(attempts, 3);
    });

    test('Does not retry write requests on timeout', () async {
      var attempts = 0;
      final mockClient = MockClient((request) async {
        attempts++;
        await Future<void>.delayed(const Duration(milliseconds: 80));
        return http.Response(jsonEncode({'run_id': 'ds-1'}), 202);
      });

      final service = TelemetryService(
        client: mockClient,
        requestTimeout: const Duration(milliseconds: 20),
        maxRetries: 3,
      );

      await expectLater(
        service.generateDataset(10),
        throwsA(predicate((e) => e.toString().contains('Request timed out'))),
      );
      expect(attempts, 1);
    });
  });
}
