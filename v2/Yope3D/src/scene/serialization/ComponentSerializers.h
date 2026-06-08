#pragma once
#include "scene/serialization/JsonWriter.h"
#include "scene/serialization/JsonParser.h"
#include "ecs/TypeId.h"

class Registry;

// Per-component serialize/deserialize functions.
// Each function writes or reads to/from a JSON object representing one component.
namespace compser {

// Transform
void serializeTransform       (const void* comp, JsonWriter& w);
bool deserializeTransform      (const JsonNode& node, void* comp);

// Hull
void serializeHull             (const void* comp, JsonWriter& w);
bool deserializeHull           (const JsonNode& node, void* comp);

// Shape forms
void serializeSphereForm       (const void* comp, JsonWriter& w);
bool deserializeSphereForm     (const JsonNode& node, void* comp);
void serializeAABBForm         (const void* comp, JsonWriter& w);
bool deserializeAABBForm       (const JsonNode& node, void* comp);
void serializeOBBForm          (const void* comp, JsonWriter& w);
bool deserializeOBBForm        (const JsonNode& node, void* comp);
void serializeCapsuleForm      (const void* comp, JsonWriter& w);
bool deserializeCapsuleForm    (const JsonNode& node, void* comp);
void serializeCylinderForm     (const void* comp, JsonWriter& w);
bool deserializeCylinderForm   (const JsonNode& node, void* comp);

// MeshRenderer (stores color; mesh path stored separately via cpuVerts/primType)
void serializeMeshRenderer     (const void* comp, JsonWriter& w);
bool deserializeMeshRenderer   (const JsonNode& node, void* comp);

// LightSource
void serializeLightSource      (const void* comp, JsonWriter& w);
bool deserializeLightSource    (const JsonNode& node, void* comp);

// Name
void serializeName             (const void* comp, JsonWriter& w);
bool deserializeName           (const JsonNode& node, void* comp);

// SpringConstraint (serializes k, restLength, target id)
void serializeSpringConstraint (const void* comp, JsonWriter& w);
bool deserializeSpringConstraint(const JsonNode& node, void* comp);

// AudioSource (path + gain/pitch/loop/autoplay; Source* recreated on load)
void serializeAudioSource      (const void* comp, JsonWriter& w);
bool deserializeAudioSource    (const JsonNode& node, void* comp);

// ScriptComponent (scriptClass + paramsBlob; live Script* is runtime-only)
void serializeScriptComponent  (const void* comp, JsonWriter& w);
bool deserializeScriptComponent(const JsonNode& node, void* comp);

// UI components (5 types; Texture*/atlas pointers are runtime-only)
void serializeUITransform           (const void* comp, JsonWriter& w);
bool deserializeUITransform          (const JsonNode& node, void* comp);
void serializeUIBackground          (const void* comp, JsonWriter& w);
bool deserializeUIBackground         (const JsonNode& node, void* comp);
void serializeUITexturedBackground  (const void* comp, JsonWriter& w);
bool deserializeUITexturedBackground (const JsonNode& node, void* comp);
void serializeUICurvedBackground    (const void* comp, JsonWriter& w);
bool deserializeUICurvedBackground   (const JsonNode& node, void* comp);
void serializeUIText                (const void* comp, JsonWriter& w);
bool deserializeUIText               (const JsonNode& node, void* comp);
void serializeTextLabel3D           (const void* comp, JsonWriter& w);
bool deserializeTextLabel3D          (const JsonNode& node, void* comp);

} // namespace compser
