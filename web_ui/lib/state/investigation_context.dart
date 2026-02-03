class InvestigationContext {
  final String datasetId;
  final String metric;
  final bool useUtc;
  final String? region;
  final String? anomalyType;
  final String? isAnomaly;
  final String? startTime;
  final String? endTime;
  final String? bucketStart;
  final String? bucketEnd;
  final int? offset;
  final int? limit;

  const InvestigationContext({
    required this.datasetId,
    required this.metric,
    required this.useUtc,
    this.region,
    this.anomalyType,
    this.isAnomaly,
    this.startTime,
    this.endTime,
    this.bucketStart,
    this.bucketEnd,
    this.offset,
    this.limit,
  });

  InvestigationContext copyWith({
    String? datasetId,
    String? metric,
    bool? useUtc,
    String? region,
    String? anomalyType,
    String? isAnomaly,
    String? startTime,
    String? endTime,
    String? bucketStart,
    String? bucketEnd,
    int? offset,
    int? limit,
  }) {
    return InvestigationContext(
      datasetId: datasetId ?? this.datasetId,
      metric: metric ?? this.metric,
      useUtc: useUtc ?? this.useUtc,
      region: region ?? this.region,
      anomalyType: anomalyType ?? this.anomalyType,
      isAnomaly: isAnomaly ?? this.isAnomaly,
      startTime: startTime ?? this.startTime,
      endTime: endTime ?? this.endTime,
      bucketStart: bucketStart ?? this.bucketStart,
      bucketEnd: bucketEnd ?? this.bucketEnd,
      offset: offset ?? this.offset,
      limit: limit ?? this.limit,
    );
  }

  Map<String, String> toQueryParams() {
    final params = <String, String>{
      'datasetId': datasetId,
      'metric': metric,
      'tz': useUtc ? 'utc' : 'local',
    };
    if (region != null) params['region'] = region!;
    if (anomalyType != null) params['anomalyType'] = anomalyType!;
    if (isAnomaly != null) params['isAnomaly'] = isAnomaly!;
    if (startTime != null) params['startTime'] = startTime!;
    if (endTime != null) params['endTime'] = endTime!;
    if (bucketStart != null) params['bucketStart'] = bucketStart!;
    if (bucketEnd != null) params['bucketEnd'] = bucketEnd!;
    if (offset != null) params['offset'] = offset.toString();
    if (limit != null) params['limit'] = limit.toString();
    return params;
  }
}
