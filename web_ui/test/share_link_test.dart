import 'package:flutter_test/flutter_test.dart';
import 'package:web_ui/state/app_state.dart';
import 'package:web_ui/utils/share_link.dart';

void main() {
  test('buildShareUri includes dataset-only context', () {
    final state = AppState();
    state.setDataset('ds-123');
    state.setSelectedMetric('ds-123', 'cpu_usage');

    final uri = buildShareUri(
      state: state,
      baseUri: Uri.parse('http://localhost:8300'),
      scope: ShareLinkScope.datasetOnly,
    );

    expect(uri.queryParameters['datasetId'], 'ds-123');
    expect(uri.queryParameters['metric'], 'cpu_usage');
    expect(uri.queryParameters.containsKey('modelId'), false);
  });

  test('buildShareUri includes dataset + model context', () {
    final state = AppState();
    state.setDataset('ds-123');
    state.setModel('model-456');

    final uri = buildShareUri(
      state: state,
      baseUri: Uri.parse('http://localhost:8300?foo=bar'),
      scope: ShareLinkScope.datasetModel,
    );

    expect(uri.queryParameters['datasetId'], 'ds-123');
    expect(uri.queryParameters['modelId'], 'model-456');
    expect(uri.queryParameters['foo'], 'bar');
  });

  test('buildShareUri handles null IDs', () {
    final state = AppState();

    final uri = buildShareUri(
      state: state,
      baseUri: Uri.parse('http://localhost:8300?datasetId=old'),
      scope: ShareLinkScope.fullContext,
    );

    expect(uri.queryParameters.containsKey('datasetId'), false);
    expect(uri.queryParameters.containsKey('modelId'), false);
  });
}
