import '../state/app_state.dart';

enum ShareLinkScope {
  currentTab,
  datasetOnly,
  datasetModel,
  fullContext,
}

Uri buildShareUri({required AppState state, required Uri baseUri, ShareLinkScope scope = ShareLinkScope.fullContext}) {
  final params = Map<String, String>.from(baseUri.queryParameters);

  void setIfNotNull(String key, String? value) {
    if (value != null && value.isNotEmpty) {
      params[key] = value;
    } else {
      params.remove(key);
    }
  }

  final datasetId = state.datasetId;
  final modelId = state.modelRunId;

  switch (scope) {
    case ShareLinkScope.currentTab:
      setIfNotNull('datasetId', datasetId);
      setIfNotNull('modelId', modelId);
      if (datasetId != null) {
        setIfNotNull('metric', state.getSelectedMetric(datasetId));
      } else {
        params.remove('metric');
      }
      break;
    case ShareLinkScope.datasetOnly:
      setIfNotNull('datasetId', datasetId);
      params.remove('modelId');
      if (datasetId != null) {
        setIfNotNull('metric', state.getSelectedMetric(datasetId));
      } else {
        params.remove('metric');
      }
      break;
    case ShareLinkScope.datasetModel:
      setIfNotNull('datasetId', datasetId);
      setIfNotNull('modelId', modelId);
      if (datasetId != null) {
        setIfNotNull('metric', state.getSelectedMetric(datasetId));
      } else {
        params.remove('metric');
      }
      break;
    case ShareLinkScope.fullContext:
      setIfNotNull('datasetId', datasetId);
      setIfNotNull('modelId', modelId);
      if (datasetId != null) {
        setIfNotNull('metric', state.getSelectedMetric(datasetId));
      } else {
        params.remove('metric');
      }
      setIfNotNull('resultsDatasetId', params['resultsDatasetId']);
      setIfNotNull('resultsModelId', params['resultsModelId']);
      break;
  }

  return baseUri.replace(queryParameters: params);
}
