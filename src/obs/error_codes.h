#pragma once

namespace telemetry {
namespace obs {

inline constexpr const char* kErrHttpBadRequest = "E_HTTP_BAD_REQUEST";
inline constexpr const char* kErrHttpNotFound = "E_HTTP_NOT_FOUND";
inline constexpr const char* kErrHttpInvalidArgument = "E_HTTP_INVALID_ARGUMENT";
inline constexpr const char* kErrHttpResourceExhausted = "E_HTTP_RESOURCE_EXHAUSTED";
inline constexpr const char* kErrHttpGrpcError = "E_HTTP_GRPC_ERROR";

inline constexpr const char* kErrDbConnectFailed = "E_DB_CONNECT_FAILED";
inline constexpr const char* kErrDbQueryFailed = "E_DB_QUERY_FAILED";
inline constexpr const char* kErrDbInsertFailed = "E_DB_INSERT_FAILED";

inline constexpr const char* kErrTrainNoData = "E_TRAIN_NO_DATA";
inline constexpr const char* kErrTrainArtifactWriteFailed = "E_TRAIN_ARTIFACT_WRITE_FAILED";
inline constexpr const char* kErrModelLoadFailed = "E_MODEL_LOAD_FAILED";
inline constexpr const char* kErrInferScoreFailed = "E_INFER_SCORE_FAILED";

inline constexpr const char* kErrInternal = "E_INTERNAL";


inline constexpr const char* kErrHttpMissingField = "E_HTTP_MISSING_FIELD";
inline constexpr const char* kErrHttpJsonParseError = "E_HTTP_JSON_PARSE_ERROR";

} // namespace obs
} // namespace telemetry
