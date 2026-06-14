// include/advdeck/schema_embed.h
//
// GENERATED FILE - DO NOT EDIT BY HAND.
//
// Source: the vendored JSON schemas under
//   projects/advdeck-agent/schemas/
// Regenerate with:
//   python3 scripts/build_schema_embed.py
//   --schema-dir projects/advdeck-agent/schemas
//   --out include/advdeck/schema_embed.h
//
// This header embeds the JSON Schemas as C string literals so the
// firmware can validate bridge results without an SD-backed file
// load (the SD impl is a stub in Phase 2/3).
//
#ifndef ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_
#define ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_

extern "C" {

// pending-request.schema.json
inline constexpr const char* kPendingRequestSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"PendingBridgeRequest\",\n  \"type\": \"object\",\n  \"required\": [\"id\", \"project\", \"type\", \"inputs\", \"created_at\", \"status\"],\n  \"properties\": {\n    \"id\":         { \"type\": \"string\", \"pattern\": \"^req-[0-9]{8}-[0-9]{3,6}$\" },\n    \"project\":    { \"type\": \"string\", \"pattern\": \"^[a-z0-9][a-z0-9-]{0,63}$\" },\n    \"type\":       { \"type\": \"string\", \"enum\": [\"plan_project\"] },\n    \"inputs\":     { \"type\": \"array\", \"items\": { \"type\": \"string\" }, \"minItems\": 1 },\n    \"created_at\": { \"type\": \"string\", \"format\": \"date-time\" },\n    \"status\":     { \"type\": \"string\", \"enum\": [\"pending\", \"in_flight\", \"done\", \"error\"] },\n    \"attempts\":   { \"type\": \"number\", \"minimum\": 0 }\n  },\n  \"additionalProperties\": false\n}\n";

// result-manifest.schema.json
inline constexpr const char* kResultManifestSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"BridgeResultManifest\",\n  \"type\": \"object\",\n  \"required\": [\"request_id\", \"status\", \"artifacts\", \"warnings\"],\n  \"properties\": {\n    \"request_id\":   { \"type\": \"string\", \"pattern\": \"^req-[0-9]{8}-[0-9]{3,6}$\" },\n    \"status\":       { \"type\": \"string\", \"enum\": [\"ok\"] },\n    \"artifacts\":    { \"type\": \"array\", \"items\": { \"type\": \"string\" }, \"minItems\": 1 },\n    \"warnings\":     { \"type\": \"array\", \"items\": { \"type\": \"string\" } }\n  },\n  \"additionalProperties\": false\n}\n";

// agent-pack-export-info.schema.json
inline constexpr const char* kAgentPackExportInfoSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"AgentPackExportInfo\",\n  \"type\": \"object\",\n  \"required\": [\"version\", \"exported_at\", \"project_slug\", \"planner_provider\", \"planner_version\"],\n  \"properties\": {\n    \"version\": { \"type\": \"number\", \"const\": 1 },\n    \"exported_at\": { \"type\": \"string\", \"format\": \"date-time\" },\n    \"project_slug\": { \"type\": \"string\", \"pattern\": \"^[a-z0-9][a-z0-9-]{0,63}$\" },\n    \"planner_provider\": { \"type\": \"string\" },\n    \"planner_version\": { \"type\": \"string\" },\n    \"request_id\": { \"type\": \"string\" },\n    \"artifact_hashes\": {\n      \"type\": \"object\",\n      \"additionalProperties\": { \"type\": \"string\" }\n    }\n  },\n  \"additionalProperties\": false\n}\n";

// recording-manifest.schema.json
inline constexpr const char* kRecordingManifestSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"RecordingManifest\",\n  \"type\": \"object\",\n  \"required\": [\"version\", \"file\", \"duration_ms\", \"sample_rate\", \"captured_at\", \"sha256\"],\n  \"properties\": {\n    \"version\":          { \"type\": \"number\", \"const\": 1 },\n    \"file\":             { \"type\": \"string\", \"pattern\": \"^recording-[0-9]+.wav$\" },\n    \"duration_ms\":      { \"type\": \"number\", \"minimum\": 0 },\n    \"sample_rate\":      { \"type\": \"number\", \"enum\": [8000, 11025, 16000, 22050, 44100, 48000] },\n    \"channels\":         { \"type\": \"number\", \"enum\": [1, 2] },\n    \"bits_per_sample\":  { \"type\": \"number\", \"enum\": [8, 16] },\n    \"captured_at\":      { \"type\": \"string\", \"format\": \"date-time\" },\n    \"sha256\":           { \"type\": \"string\", \"pattern\": \"^[0-9a-f]{64}$\" }\n  },\n  \"additionalProperties\": false\n}\n";

}  // extern "C"

#endif  // ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_
