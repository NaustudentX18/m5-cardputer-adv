// include/advdeck/schema_embed.h
//
// GENERATED FILE - DO NOT EDIT BY HAND.
//
// Source: the two vendored JSON schemas under
//   projects/advdeck-agent/schemas/
// Regenerate with:
//   python3 scripts/build_schema_embed.py
//   --schema-dir projects/advdeck-agent/schemas
//   --out include/advdeck/schema_embed.h
//
// This header embeds the JSON Schemas as C string literals so the
// firmware can validate bridge results without an SD-backed file
// load (the SD impl is a stub in Phase 2).
//
#ifndef ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_
#define ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_

extern "C" {

// pending-request.schema.json
inline constexpr const char* kPendingRequestSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"PendingBridgeRequest\",\n  \"type\": \"object\",\n  \"required\": [\"id\", \"project\", \"type\", \"inputs\", \"created_at\", \"status\"],\n  \"properties\": {\n    \"id\":         { \"type\": \"string\", \"pattern\": \"^req-[0-9]{8}-[0-9]{3,6}$\" },\n    \"project\":    { \"type\": \"string\", \"pattern\": \"^[a-z0-9][a-z0-9-]{0,63}$\" },\n    \"type\":       { \"type\": \"string\", \"enum\": [\"plan_project\"] },\n    \"inputs\":     { \"type\": \"array\", \"items\": { \"type\": \"string\" }, \"minItems\": 1 },\n    \"created_at\": { \"type\": \"string\", \"format\": \"date-time\" },\n    \"status\":     { \"type\": \"string\", \"enum\": [\"pending\", \"in_flight\", \"done\", \"error\"] },\n    \"attempts\":   { \"type\": \"number\", \"minimum\": 0 }\n  },\n  \"additionalProperties\": false\n}\n";

// result-manifest.schema.json
inline constexpr const char* kResultManifestSchema = "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"title\": \"BridgeResultManifest\",\n  \"type\": \"object\",\n  \"required\": [\"request_id\", \"status\", \"artifacts\", \"warnings\"],\n  \"properties\": {\n    \"request_id\":   { \"type\": \"string\", \"pattern\": \"^req-[0-9]{8}-[0-9]{3,6}$\" },\n    \"status\":       { \"type\": \"string\", \"enum\": [\"ok\"] },\n    \"artifacts\":    { \"type\": \"array\", \"items\": { \"type\": \"string\" }, \"minItems\": 1 },\n    \"warnings\":     { \"type\": \"array\", \"items\": { \"type\": \"string\" } }\n  },\n  \"additionalProperties\": false\n}\n";

}  // extern "C"

#endif  // ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_
