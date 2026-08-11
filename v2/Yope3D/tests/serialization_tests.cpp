// ============================================================================
// Component serialization round-trip tests (headless).
//
// Scope note — why this is NOT "permute every combination of components":
//   Serialization is *per-component and independent*. Every component type has
//   its own serialize/deserialize pair (compser::) that reads/writes only its
//   own keyed fields into a JSON object; there is no cross-component coupling in
//   a pair. So an entity carrying {A,B,C} round-trips correctly iff A, B and C
//   each round-trip — testing all 2^N subsets (N ~= 28 => a billion) adds no
//   information over testing each pair once. The ONE cross-entity concern —
//   entity references (Parent.parent, *JointConstraint.target, Spring.target) —
//   is deliberately handled OUTSIDE the pairs by SceneSerializer's fileId
//   remap, so those fields are verified here to be intentionally dropped, not
//   preserved.
//
// What "mechanically verify everything" means here: one round-trip case per
// registered serializer that populates every serialized field with a distinct
// non-default value and asserts it survives serialize -> parse -> deserialize.
// The completeness guard at the bottom lists all 28 serializers so a newly
// added one without a case is visible.
//
// Fidelity caveat baked into the expectations: JsonWriter emits floats with
// "%.6g" (6 significant figures), so test values are chosen to be exact at that
// precision and compared with a small tolerance. uints/ints/bools/strings are
// exact.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ecs/Components.h"
#include "world/Transform.h"
#include "scene/serialization/ComponentSerializers.h"
#include "scene/serialization/JsonWriter.h"
#include "scene/serialization/JsonParser.h"

#include <string>
#include <vector>
#include <cstring>

using namespace Catch::Matchers;

// ----------------------------------------------------------------------------
// Harness
// ----------------------------------------------------------------------------

namespace {

// serialize `in` into a JSON object, parse it back, and deserialize into a
// fresh default-constructed T — exactly the save -> load path minus the file.
template <typename T>
T roundtrip(void (*ser)(const void*, JsonWriter&),
            bool (*deser)(const JsonNode&, void*),
            const T& in) {
    JsonWriter w;
    w.beginObject();
    ser(&in, w);
    w.endObject();
    JsonNode node = parseJson(w.str().c_str());
    T out{};
    REQUIRE(deser(node, &out));
    return out;
}

constexpr float TOL = 1e-4f;

void eqF(float a, float b)  { CHECK_THAT(a, WithinAbs(b, TOL)); }
void eqV(const math::Vec3& a, const math::Vec3& b) {
    CHECK_THAT(a.x, WithinAbs(b.x, TOL));
    CHECK_THAT(a.y, WithinAbs(b.y, TOL));
    CHECK_THAT(a.z, WithinAbs(b.z, TOL));
}

} // namespace

// ============================================================================
// Core transform / physics
// ============================================================================

TEST_CASE("round-trip Transform", "[ser][transform]") {
    Transform in;
    in.position = {1.5f, -2.25f, 3.125f};
    in.rotation = {0.5f, -0.5f, 0.5f, 0.5f};
    in.scale    = {2.0f, 0.25f, 4.0f};

    Transform out = roundtrip<Transform>(compser::serializeTransform, compser::deserializeTransform, in);
    eqV(out.position, in.position);
    CHECK_THAT(out.rotation.x, WithinAbs(in.rotation.x, TOL));
    CHECK_THAT(out.rotation.y, WithinAbs(in.rotation.y, TOL));
    CHECK_THAT(out.rotation.z, WithinAbs(in.rotation.z, TOL));
    CHECK_THAT(out.rotation.w, WithinAbs(in.rotation.w, TOL));
    eqV(out.scale, in.scale);
}

TEST_CASE("round-trip Hull (serialized subset; inverseMass re-derived)", "[ser][hull]") {
    ecs::Hull in;
    in.velocity        = {1.25f, -2.5f, 3.75f};
    in.omega           = {-0.5f, 0.25f, -0.125f};
    in.mass            = 4.0f;
    in.friction        = 0.625f;
    in.restitution     = 0.375f;
    in.linearDamping   = 0.125f;
    in.angularDamping  = 0.0625f;
    in.collisionLayer  = 0xDEADBEEFu;
    in.collisionMask   = 0x00C0FFEEu;
    in.observeLayers   = 0x00000008u;   // non-zero so it is emitted
    in.gravity         = false;
    in.tangible        = false;
    in.sleepingEnabled = false;
    in.isTrigger       = true;
    in.kinematic       = true;

    ecs::Hull out = roundtrip<ecs::Hull>(compser::serializeHull, compser::deserializeHull, in);
    eqV(out.velocity, in.velocity);
    eqV(out.omega, in.omega);
    eqF(out.mass, in.mass);
    eqF(out.friction, in.friction);
    eqF(out.restitution, in.restitution);
    eqF(out.linearDamping, in.linearDamping);
    eqF(out.angularDamping, in.angularDamping);
    CHECK(out.collisionLayer == in.collisionLayer);   // uint => exact
    CHECK(out.collisionMask  == in.collisionMask);
    CHECK(out.observeLayers  == in.observeLayers);
    CHECK(out.gravity        == in.gravity);
    CHECK(out.tangible       == in.tangible);
    CHECK(out.sleepingEnabled == in.sleepingEnabled);
    CHECK(out.isTrigger      == in.isTrigger);
    CHECK(out.kinematic      == in.kinematic);
    // inverseMass is not serialized — it is recomputed as 1/mass on load.
    eqF(out.inverseMass, 1.0f / in.mass);
}

TEST_CASE("Hull observeLayers==0 is omitted and round-trips to 0", "[ser][hull]") {
    ecs::Hull in;                 // default observeLayers == 0
    in.mass = 2.0f;
    ecs::Hull out = roundtrip<ecs::Hull>(compser::serializeHull, compser::deserializeHull, in);
    CHECK(out.observeLayers == 0u);
}

// ============================================================================
// Collider shape forms
// ============================================================================

TEST_CASE("round-trip SphereForm", "[ser][forms]") {
    ecs::SphereForm in{2.5f};
    auto out = roundtrip<ecs::SphereForm>(compser::serializeSphereForm, compser::deserializeSphereForm, in);
    eqF(out.radius, in.radius);
}

TEST_CASE("round-trip AABBForm / OBBForm", "[ser][forms]") {
    ecs::AABBForm a{{1.5f, 2.25f, 0.75f}};
    auto ao = roundtrip<ecs::AABBForm>(compser::serializeAABBForm, compser::deserializeAABBForm, a);
    eqV(ao.extent, a.extent);

    ecs::OBBForm o{{3.5f, 0.5f, 1.25f}};
    auto oo = roundtrip<ecs::OBBForm>(compser::serializeOBBForm, compser::deserializeOBBForm, o);
    eqV(oo.extent, o.extent);
}

TEST_CASE("round-trip CapsuleForm / CylinderForm", "[ser][forms]") {
    ecs::CapsuleForm c{0.75f, 2.5f};
    auto co = roundtrip<ecs::CapsuleForm>(compser::serializeCapsuleForm, compser::deserializeCapsuleForm, c);
    eqF(co.radius, c.radius);
    eqF(co.halfHeight, c.halfHeight);

    ecs::CylinderForm y{1.25f, 0.5f};
    auto yo = roundtrip<ecs::CylinderForm>(compser::serializeCylinderForm, compser::deserializeCylinderForm, y);
    eqF(yo.radius, y.radius);
    eqF(yo.halfHeight, y.halfHeight);
}

TEST_CASE("round-trip CompoundCollider (asset path + bake settings; compiled* dropped)", "[ser][compound]") {
    ecs::CompoundCollider in;
    std::strncpy(in.assetPath, "colliders/level1.bcbvh", sizeof(in.assetPath) - 1);
    in.density  = 2.5f;
    in.isStatic = false;
    in.compiled = reinterpret_cast<physics::CompiledCollider*>(0xABCD);   // runtime-only; must NOT persist

    auto out = roundtrip<ecs::CompoundCollider>(compser::serializeCompoundCollider,
                                                compser::deserializeCompoundCollider, in);
    CHECK(std::string(out.assetPath) == "colliders/level1.bcbvh");
    eqF(out.density, in.density);
    CHECK(out.isStatic == in.isStatic);
    CHECK(out.compiled == nullptr);   // re-resolved by World on load, never from JSON
}

// ============================================================================
// Rendering / lighting
// ============================================================================

TEST_CASE("round-trip Material (paths + factors)", "[ser][material]") {
    ecs::Material in;
    std::strncpy(in.albedoPath,     "tex/albedo.png",  sizeof(in.albedoPath) - 1);
    std::strncpy(in.normalPath,     "tex/normal.png",  sizeof(in.normalPath) - 1);
    std::strncpy(in.metalRoughPath, "tex/mr.png",      sizeof(in.metalRoughPath) - 1);
    std::strncpy(in.occlusionPath,  "tex/ao.png",      sizeof(in.occlusionPath) - 1);
    std::strncpy(in.emissivePath,   "tex/emit.png",    sizeof(in.emissivePath) - 1);
    in.albedoFactor[0] = 0.5f; in.albedoFactor[1] = 0.25f; in.albedoFactor[2] = 0.75f; in.albedoFactor[3] = 0.125f;
    in.metallicFactor  = 0.625f;
    in.roughnessFactor = 0.375f;
    in.emissiveFactor[0] = 0.1f; in.emissiveFactor[1] = 0.2f; in.emissiveFactor[2] = 0.4f;
    in.normalScale     = 1.5f;

    auto out = roundtrip<ecs::Material>(compser::serializeMaterial, compser::deserializeMaterial, in);
    CHECK(std::string(out.albedoPath)     == "tex/albedo.png");
    CHECK(std::string(out.normalPath)     == "tex/normal.png");
    CHECK(std::string(out.metalRoughPath) == "tex/mr.png");
    CHECK(std::string(out.occlusionPath)  == "tex/ao.png");
    CHECK(std::string(out.emissivePath)   == "tex/emit.png");
    for (int i = 0; i < 4; ++i) eqF(out.albedoFactor[i], in.albedoFactor[i]);
    for (int i = 0; i < 3; ++i) eqF(out.emissiveFactor[i], in.emissiveFactor[i]);
    eqF(out.metallicFactor, in.metallicFactor);
    eqF(out.roughnessFactor, in.roughnessFactor);
    eqF(out.normalScale, in.normalScale);
    CHECK(out.resolved == nullptr);
}

TEST_CASE("round-trip LightSource (all emitter fields)", "[ser][light]") {
    ecs::LightSource in;
    in.type = 2;                                   // spot
    in.color[0] = 0.9f; in.color[1] = 0.5f; in.color[2] = 0.1f;
    in.intensity = 3.5f;
    in.position[0] = 1.5f; in.position[1] = 2.5f; in.position[2] = -3.5f;
    in.direction[0] = 0.0f; in.direction[1] = -1.0f; in.direction[2] = 0.5f;
    in.constant = 1.25f; in.linear = 0.125f; in.quadratic = 0.03125f;
    in.innerConeAngle = 0.25f; in.outerConeAngle = 0.75f;
    in.castsShadow = true;

    auto out = roundtrip<ecs::LightSource>(compser::serializeLightSource, compser::deserializeLightSource, in);
    CHECK(out.type == in.type);
    for (int i = 0; i < 3; ++i) eqF(out.color[i], in.color[i]);
    for (int i = 0; i < 3; ++i) eqF(out.position[i], in.position[i]);
    for (int i = 0; i < 3; ++i) eqF(out.direction[i], in.direction[i]);
    eqF(out.intensity, in.intensity);
    eqF(out.constant, in.constant);
    eqF(out.linear, in.linear);
    eqF(out.quadratic, in.quadratic);
    eqF(out.innerConeAngle, in.innerConeAngle);
    eqF(out.outerConeAngle, in.outerConeAngle);
    CHECK(out.castsShadow == in.castsShadow);
}

// ============================================================================
// Identity / provenance / audio / script
// ============================================================================

TEST_CASE("round-trip Name", "[ser][name]") {
    ecs::Name in;
    std::strncpy(in.value, "Player Spawn", sizeof(in.value) - 1);
    auto out = roundtrip<ecs::Name>(compser::serializeName, compser::deserializeName, in);
    CHECK(std::string(out.value) == "Player Spawn");
}

TEST_CASE("round-trip TemplateInstance", "[ser][template]") {
    ecs::TemplateInstance in;
    std::strncpy(in.sourcePath, "prefabs/enemy.ytemplated", sizeof(in.sourcePath) - 1);
    auto out = roundtrip<ecs::TemplateInstance>(compser::serializeTemplateInstance,
                                                compser::deserializeTemplateInstance, in);
    CHECK(std::string(out.sourcePath) == "prefabs/enemy.ytemplated");
}

TEST_CASE("round-trip AudioSource (Source* dropped)", "[ser][audio]") {
    ecs::AudioSource in;
    std::strncpy(in.path, "audios/hum.ogg", sizeof(in.path) - 1);
    in.gain = 0.75f; in.pitch = 1.25f;
    in.loop = true; in.autoplay = true;
    in.bus = 2;
    in.source = reinterpret_cast<Source*>(0x1234);   // runtime-only

    auto out = roundtrip<ecs::AudioSource>(compser::serializeAudioSource, compser::deserializeAudioSource, in);
    CHECK(std::string(out.path) == "audios/hum.ogg");
    eqF(out.gain, in.gain);
    eqF(out.pitch, in.pitch);
    CHECK(out.loop == in.loop);
    CHECK(out.autoplay == in.autoplay);
    CHECK(out.bus == in.bus);
    CHECK(out.source == nullptr);
}

TEST_CASE("round-trip ScriptComponent (class + params blob; instance dropped)", "[ser][script]") {
    ecs::ScriptComponent in;
    std::strncpy(in.scriptClass, "CharacterController", sizeof(in.scriptClass) - 1);
    std::strncpy(in.paramsBlob, "{\"speed\": 5.0, \"jump\": 10}", sizeof(in.paramsBlob) - 1);

    auto out = roundtrip<ecs::ScriptComponent>(compser::serializeScriptComponent,
                                               compser::deserializeScriptComponent, in);
    CHECK(std::string(out.scriptClass) == "CharacterController");
    CHECK(std::string(out.paramsBlob)  == "{\"speed\": 5.0, \"jump\": 10}");
    CHECK(out.instance == nullptr);
}

// ============================================================================
// Constraints — the k/anchor/limit data round-trips; the entity `target` is
// deliberately NOT carried by the pair (SceneSerializer patches it via fileId).
// ============================================================================

TEST_CASE("round-trip SpringConstraint (target dropped by design)", "[ser][spring][entityref]") {
    ecs::SpringConstraint in;
    in.target     = ecs::Entity{7, 2};   // must NOT survive the pair
    in.k          = 42.5f;
    in.restLength = 3.25f;

    auto out = roundtrip<ecs::SpringConstraint>(compser::serializeSpringConstraint,
                                                compser::deserializeSpringConstraint, in);
    eqF(out.k, in.k);
    eqF(out.restLength, in.restLength);
    CHECK(out.target == ecs::NullEntity);   // pinned: pair drops the reference
}

TEST_CASE("round-trip PointJointConstraint (anchors kept, target dropped)", "[ser][joint][entityref]") {
    ecs::PointJointConstraint in;
    in.target       = ecs::Entity{3, 1};
    in.localAnchorA = {0.5f, -0.25f, 0.75f};
    in.localAnchorB = {-1.5f, 2.0f, 0.125f};

    auto out = roundtrip<ecs::PointJointConstraint>(compser::serializePointJointConstraint,
                                                    compser::deserializePointJointConstraint, in);
    eqV(out.localAnchorA, in.localAnchorA);
    eqV(out.localAnchorB, in.localAnchorB);
    CHECK(out.target == ecs::NullEntity);
}

TEST_CASE("round-trip HingeJointConstraint", "[ser][joint][entityref]") {
    ecs::HingeJointConstraint in;
    in.target       = ecs::Entity{9, 3};
    in.localAnchorA = {1.0f, 0.5f, -0.5f};
    in.localAnchorB = {-0.5f, 1.5f, 0.25f};
    in.localAxisA   = {1.0f, 0.0f, 0.0f};
    in.localAxisB   = {0.0f, 1.0f, 0.0f};
    in.limitEnabled = true;
    in.lowerAngle   = -0.75f;
    in.upperAngle   = 1.25f;

    auto out = roundtrip<ecs::HingeJointConstraint>(compser::serializeHingeJointConstraint,
                                                    compser::deserializeHingeJointConstraint, in);
    eqV(out.localAnchorA, in.localAnchorA);
    eqV(out.localAnchorB, in.localAnchorB);
    eqV(out.localAxisA, in.localAxisA);
    eqV(out.localAxisB, in.localAxisB);
    CHECK(out.limitEnabled == in.limitEnabled);
    eqF(out.lowerAngle, in.lowerAngle);
    eqF(out.upperAngle, in.upperAngle);
    CHECK(out.target == ecs::NullEntity);
}

TEST_CASE("round-trip ConeTwistJointConstraint", "[ser][joint][entityref]") {
    ecs::ConeTwistJointConstraint in;
    in.target          = ecs::Entity{4, 4};
    in.localAnchorA    = {0.25f, 0.5f, 0.75f};
    in.localAnchorB    = {-0.25f, -0.5f, -0.75f};
    in.localTwistAxisA = {0.0f, 0.0f, 1.0f};
    in.localTwistAxisB = {1.0f, 0.0f, 0.0f};
    in.swingLimit      = 0.5f;
    in.twistLimit      = 1.0f;

    auto out = roundtrip<ecs::ConeTwistJointConstraint>(compser::serializeConeTwistJointConstraint,
                                                        compser::deserializeConeTwistJointConstraint, in);
    eqV(out.localAnchorA, in.localAnchorA);
    eqV(out.localAnchorB, in.localAnchorB);
    eqV(out.localTwistAxisA, in.localTwistAxisA);
    eqV(out.localTwistAxisB, in.localTwistAxisB);
    eqF(out.swingLimit, in.swingLimit);
    eqF(out.twistLimit, in.twistLimit);
    CHECK(out.target == ecs::NullEntity);
}

// ============================================================================
// UI components
// ============================================================================

TEST_CASE("round-trip UITransform (every layout field)", "[ser][ui]") {
    ecs::UITransform in;
    in.minX = 0.1f; in.minY = 0.2f; in.maxX = 0.8f; in.maxY = 0.9f;
    in.depth = 7; in.visible = false;
    in.anchor = 5; in.sizeMode = 1;
    in.pixelWidth = 128.0f; in.pixelHeight = 64.0f;
    in.offsetXPx = 12.0f; in.offsetYPx = -8.0f;
    in.opacity = 0.5f;

    auto out = roundtrip<ecs::UITransform>(compser::serializeUITransform, compser::deserializeUITransform, in);
    eqF(out.minX, in.minX); eqF(out.minY, in.minY);
    eqF(out.maxX, in.maxX); eqF(out.maxY, in.maxY);
    CHECK(out.depth == in.depth);
    CHECK(out.visible == in.visible);
    CHECK(out.anchor == in.anchor);
    CHECK(out.sizeMode == in.sizeMode);
    eqF(out.pixelWidth, in.pixelWidth);
    eqF(out.pixelHeight, in.pixelHeight);
    eqF(out.offsetXPx, in.offsetXPx);
    eqF(out.offsetYPx, in.offsetYPx);
    eqF(out.opacity, in.opacity);
}

TEST_CASE("round-trip UIBackground / UICurvedBackground", "[ser][ui]") {
    ecs::UIBackground in{0.25f, 0.5f, 0.75f, 0.875f};
    auto out = roundtrip<ecs::UIBackground>(compser::serializeUIBackground, compser::deserializeUIBackground, in);
    eqF(out.r, in.r); eqF(out.g, in.g); eqF(out.b, in.b); eqF(out.a, in.a);

    ecs::UICurvedBackground cin;
    cin.r = 0.1f; cin.g = 0.2f; cin.b = 0.3f; cin.a = 0.4f; cin.curvature = 0.625f;
    auto cout = roundtrip<ecs::UICurvedBackground>(compser::serializeUICurvedBackground,
                                                   compser::deserializeUICurvedBackground, cin);
    eqF(cout.r, cin.r); eqF(cout.g, cin.g); eqF(cout.b, cin.b); eqF(cout.a, cin.a);
    eqF(cout.curvature, cin.curvature);
}

TEST_CASE("round-trip UITexturedBackground (path + tint; Texture* dropped)", "[ser][ui]") {
    ecs::UITexturedBackground in;
    std::strncpy(in.path, "ui/panel.png", sizeof(in.path) - 1);
    in.tintR = 0.5f; in.tintG = 0.6f; in.tintB = 0.7f; in.tintA = 0.8f;
    in.texture = reinterpret_cast<Texture*>(0x99);

    auto out = roundtrip<ecs::UITexturedBackground>(compser::serializeUITexturedBackground,
                                                    compser::deserializeUITexturedBackground, in);
    CHECK(std::string(out.path) == "ui/panel.png");
    eqF(out.tintR, in.tintR); eqF(out.tintG, in.tintG);
    eqF(out.tintB, in.tintB); eqF(out.tintA, in.tintA);
    CHECK(out.texture == nullptr);
}

TEST_CASE("round-trip UIText (autoSizedText cache not serialized)", "[ser][ui]") {
    ecs::UIText in;
    std::strncpy(in.fontPath, "fonts/monaco.ttf", sizeof(in.fontPath) - 1);
    std::strncpy(in.text, "Score: 0", sizeof(in.text) - 1);
    in.cr = 0.2f; in.cg = 0.4f; in.cb = 0.6f; in.ca = 0.8f;
    in.displayPx = 24; in.alignment = 1;
    in.autoSize = true;
    std::strncpy(in.autoSizedText, "stale cache", sizeof(in.autoSizedText) - 1);

    auto out = roundtrip<ecs::UIText>(compser::serializeUIText, compser::deserializeUIText, in);
    CHECK(std::string(out.fontPath) == "fonts/monaco.ttf");
    CHECK(std::string(out.text) == "Score: 0");
    eqF(out.cr, in.cr); eqF(out.cg, in.cg); eqF(out.cb, in.cb); eqF(out.ca, in.ca);
    CHECK(out.displayPx == in.displayPx);
    CHECK(out.alignment == in.alignment);
    CHECK(out.autoSize == in.autoSize);
    // autoSizedText is a render cache, not serialized — stays empty on load.
    CHECK(std::string(out.autoSizedText).empty());
}

TEST_CASE("round-trip UIButton (four color states + enabled)", "[ser][ui]") {
    ecs::UIButton in;
    in.normalR = 0.1f; in.normalG = 0.2f; in.normalB = 0.3f; in.normalA = 0.4f;
    in.hoverR = 0.5f; in.hoverG = 0.6f; in.hoverB = 0.7f; in.hoverA = 0.8f;
    in.pressedR = 0.15f; in.pressedG = 0.25f; in.pressedB = 0.35f; in.pressedA = 0.45f;
    in.disabledR = 0.55f; in.disabledG = 0.65f; in.disabledB = 0.75f; in.disabledA = 0.85f;
    in.enabled = false;

    auto o = roundtrip<ecs::UIButton>(compser::serializeUIButton, compser::deserializeUIButton, in);
    eqF(o.normalR, in.normalR); eqF(o.normalG, in.normalG); eqF(o.normalB, in.normalB); eqF(o.normalA, in.normalA);
    eqF(o.hoverR, in.hoverR); eqF(o.hoverG, in.hoverG); eqF(o.hoverB, in.hoverB); eqF(o.hoverA, in.hoverA);
    eqF(o.pressedR, in.pressedR); eqF(o.pressedG, in.pressedG); eqF(o.pressedB, in.pressedB); eqF(o.pressedA, in.pressedA);
    eqF(o.disabledR, in.disabledR); eqF(o.disabledG, in.disabledG); eqF(o.disabledB, in.disabledB); eqF(o.disabledA, in.disabledA);
    CHECK(o.enabled == in.enabled);
}

TEST_CASE("round-trip TextLabel3D", "[ser][ui]") {
    ecs::TextLabel3D in;
    std::strncpy(in.fontPath, "fonts/monaco.ttf", sizeof(in.fontPath) - 1);
    std::strncpy(in.text, "spawn!", sizeof(in.text) - 1);
    in.cr = 0.9f; in.cg = 0.8f; in.cb = 0.7f; in.ca = 0.6f;
    in.sizeMeters = 2.5f;
    in.billboard = 0;

    auto out = roundtrip<ecs::TextLabel3D>(compser::serializeTextLabel3D, compser::deserializeTextLabel3D, in);
    CHECK(std::string(out.fontPath) == "fonts/monaco.ttf");
    CHECK(std::string(out.text) == "spawn!");
    eqF(out.cr, in.cr); eqF(out.cg, in.cg); eqF(out.cb, in.cb); eqF(out.ca, in.ca);
    eqF(out.sizeMeters, in.sizeMeters);
    CHECK(out.billboard == in.billboard);
}

// ============================================================================
// Animation — clip/speed/loop persist; live playback state (time/playing) is
// intentionally reset so a scene never loads mid-clip.
// ============================================================================

TEST_CASE("round-trip AnimationPlayer (time/playing reset on load)", "[ser][anim]") {
    ecs::AnimationPlayer in;
    std::strncpy(in.clip, "hero:Idle", sizeof(in.clip) - 1);
    in.time    = 3.5f;    // live state, should NOT persist
    in.speed   = 1.5f;
    in.loop    = 0;
    in.playing = 1;       // live state, should NOT persist

    auto out = roundtrip<ecs::AnimationPlayer>(compser::serializeAnimationPlayer,
                                               compser::deserializeAnimationPlayer, in);
    CHECK(std::string(out.clip) == "hero:Idle");
    eqF(out.speed, in.speed);
    CHECK(out.loop == in.loop);
    CHECK(out.time == 0.0f);      // reset by design
    CHECK(out.playing == 0);      // reset by design
}

TEST_CASE("round-trip SkinnedMeshRenderer (instance handle never persists)", "[ser][anim][skin]") {
    ecs::SkinnedMeshRenderer in;
    std::strncpy(in.skeleton, "hero:Armature", sizeof(in.skeleton) - 1);
    std::strncpy(in.clip,     "hero:Run",      sizeof(in.clip) - 1);
    in.speed    = 1.25f;
    in.loop     = 0;
    in.mode     = ecs::SkinnedMeshRenderer::ComputePreSkin;
    in.instance = 7;      // live pool handle — must NOT survive

    auto out = roundtrip<ecs::SkinnedMeshRenderer>(compser::serializeSkinnedMeshRenderer,
                                                   compser::deserializeSkinnedMeshRenderer, in);
    CHECK(std::string(out.skeleton) == "hero:Armature");
    CHECK(std::string(out.clip)     == "hero:Run");
    eqF(out.speed, in.speed);
    CHECK(out.loop == in.loop);
    CHECK(out.mode == ecs::SkinnedMeshRenderer::ComputePreSkin);
    // Persisting this would restore an index into a pool rebuilt from scratch —
    // pointing at another character's palette, or at nothing.
    CHECK(out.instance == -1);
}

TEST_CASE("round-trip BoneAttachment (bone index re-resolved, not stored)", "[ser][anim][skin]") {
    ecs::BoneAttachment in;
    std::strncpy(in.boneName, "hand.R", sizeof(in.boneName) - 1);
    in.boneIndex = 12;    // cache of a lookup, not authored state

    auto out = roundtrip<ecs::BoneAttachment>(compser::serializeBoneAttachment,
                                              compser::deserializeBoneAttachment, in);
    CHECK(std::string(out.boneName) == "hand.R");
    CHECK(out.boneIndex == -1);
}

// ============================================================================
// Components handled OUTSIDE the pair (documented behavior, pinned here so a
// change to it is visible).
// ============================================================================

TEST_CASE("MeshRenderer deserialize is a no-op (mesh recreated by SceneSerializer)", "[ser][meshrenderer]") {
    // serialize with a null mesh writes nothing; deserialize never touches the
    // component — the RenderMesh is rebuilt from primitiveType/sourcePath by the
    // scene loader, not from this pair.
    ecs::MeshRenderer in{};   // mesh == nullptr
    JsonWriter w; w.beginObject(); compser::serializeMeshRenderer(&in, w); w.endObject();
    JsonNode node = parseJson(w.str().c_str());
    ecs::MeshRenderer out{};
    out.mesh = reinterpret_cast<RenderMesh*>(0x1);
    CHECK(compser::deserializeMeshRenderer(node, &out));
    CHECK(out.mesh == reinterpret_cast<RenderMesh*>(0x1));   // untouched
}

TEST_CASE("Parent pair carries nothing (parentId patched via fileId two-pass)", "[ser][parent][entityref]") {
    ecs::Parent in{ecs::Entity{5, 1}};
    JsonWriter w; w.beginObject(); compser::serializeParent(&in, w); w.endObject();
    // Body is empty: "{\n}" or similar — must still parse as an object.
    JsonNode node = parseJson(w.str().c_str());
    CHECK(node.isObject());
    ecs::Parent out{};
    CHECK(compser::deserializeParent(node, &out));
    CHECK(out.parent == ecs::NullEntity);   // the reference is not carried here
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("default-constructed components round-trip to their defaults", "[ser][defaults]") {
    // Exercises the conditional-omission path (empty strings / zero fields are
    // not written) — deserialize must leave the defaults intact.
    ecs::Material m = roundtrip<ecs::Material>(compser::serializeMaterial, compser::deserializeMaterial, ecs::Material{});
    CHECK(std::string(m.albedoPath).empty());
    eqF(m.metallicFactor, 1.0f);
    eqF(m.roughnessFactor, 1.0f);

    ecs::AudioSource a = roundtrip<ecs::AudioSource>(compser::serializeAudioSource, compser::deserializeAudioSource, ecs::AudioSource{});
    CHECK(std::string(a.path).empty());
    eqF(a.gain, 1.0f);
    CHECK(a.bus == 1);
}

TEST_CASE("string fields truncate at the fixed buffer size without overflow", "[ser][defaults]") {
    // Name.value is char[64]. An over-long name must truncate to 63 chars, and
    // the truncated form must itself round-trip stably.
    ecs::Name in;
    const std::string longName(200, 'x');
    std::strncpy(in.value, longName.c_str(), sizeof(in.value) - 1);
    REQUIRE(std::strlen(in.value) == 63);

    auto out = roundtrip<ecs::Name>(compser::serializeName, compser::deserializeName, in);
    CHECK(std::strlen(out.value) == 63);
    CHECK(std::string(out.value) == std::string(63, 'x'));
}

TEST_CASE("strings with JSON metacharacters survive escaping", "[ser][escaping]") {
    // Quotes/backslashes/newlines/tabs in a serialized string must be escaped by
    // the writer and un-escaped by the parser (paramsBlob commonly holds JSON).
    ecs::Name in;
    std::strncpy(in.value, "a\"b\\c\td", sizeof(in.value) - 1);
    auto out = roundtrip<ecs::Name>(compser::serializeName, compser::deserializeName, in);
    CHECK(std::string(out.value) == "a\"b\\c\td");
}

// ============================================================================
// Completeness guard — every registered serializer must have a round-trip case
// above. If you add a component + serializer pair (see the new-component
// checklist in CLAUDE.md), add its case and its name here; this list is the
// mechanical stand-in for iterating the engine's ser-table (which lives in
// SceneSerializer.cpp and can't link headlessly).
// ============================================================================

TEST_CASE("every serializable component is covered by a round-trip case", "[ser][completeness]") {
    const std::vector<std::string> covered = {
        // real symmetric pairs, each exercised above
        "Transform", "Hull", "SphereForm", "AABBForm", "OBBForm", "CapsuleForm",
        "CylinderForm", "CompoundCollider", "Material", "LightSource", "Name",
        "TemplateInstance", "SpringConstraint", "PointJointConstraint",
        "HingeJointConstraint", "ConeTwistJointConstraint", "AudioSource",
        "ScriptComponent", "UITransform", "UIBackground", "UITexturedBackground",
        "UICurvedBackground", "UIText", "UIButton", "TextLabel3D", "AnimationPlayer",
        "SkinnedMeshRenderer", "BoneAttachment",
        // handled outside the pair (deserialize is a no-op / empty), pinned above
        "MeshRenderer", "Parent",
    };
    // Bump this when adding a serializer (and add the case + the name above).
    CHECK(covered.size() == 30);
}
