#include "scripting/CompositeScript.h"
#include "scripting/ScriptFactory.h"
#include "scene/serialization/JsonParser.h"
#include "scene/serialization/JsonWriter.h"
#include <cstdio>

YOPE_REGISTER_SCRIPT(CompositeScript);

// ---------------------------------------------------------------------------
// Params — build one child Script per entry in the "scripts" array
// ---------------------------------------------------------------------------

bool CompositeScript::deserializeParams(const JsonNode& n) {
    children_.clear();
    childSpecs_.clear();
    if (!n.contains("scripts") || !n["scripts"].isArray()) {
        std::fprintf(stderr, "CompositeScript: paramsBlob has no \"scripts\" array\n");
        return false;
    }
    for (const auto& elem : n["scripts"].asArray()) {
        std::string cls = elem.contains("class") ? elem["class"].asString() : "";
        if (cls.empty()) {
            std::fprintf(stderr, "CompositeScript: a child spec is missing \"class\" -- skipped\n");
            continue;
        }
        // Duplicate class names are rejected: the class name is the per-entity
        // identity key for get_behavior and for save-state (composite-behaviors.md
        // §4.1), so it must be unique on one entity.
        bool dup = false;
        for (auto& ch : children_) if (ch->behaviorClassName() == cls) { dup = true; break; }
        if (dup) {
            std::fprintf(stderr, "CompositeScript: duplicate behavior class '%s' on one entity -- ignored\n",
                         cls.c_str());
            continue;
        }
        // Children are always PythonScripts (v1). Going through the factory keeps
        // this translation unit free of any Python dependency — if the Python
        // runtime is compiled out, create() returns null and the child is skipped.
        auto child = ScriptFactory::create("PythonScript");
        if (!child) {
            std::fprintf(stderr, "CompositeScript: PythonScript factory unavailable -- '%s' skipped\n",
                         cls.c_str());
            continue;
        }
        child->deserializeParams(elem);   // reads module/class + nested "params"
        children_.push_back(std::move(child));
        childSpecs_.push_back(dumpJson(elem));
    }
    return !children_.empty();
}

// ---------------------------------------------------------------------------
// Runtime composition
// ---------------------------------------------------------------------------

Script* CompositeScript::appendChild(std::unique_ptr<Script> child, std::string specJson) {
    if (!child) return nullptr;
    std::string cls = child->behaviorClassName();
    if (!cls.empty())
        for (auto& ch : children_)
            if (ch && ch->behaviorClassName() == cls) return nullptr;   // duplicate class
    Script* raw = child.get();
    children_.push_back(std::move(child));
    childSpecs_.push_back(std::move(specJson));
    return raw;
}

std::string CompositeScript::composeBlob() const {
    std::string blob = "{\"scripts\":[";
    for (size_t i = 0; i < childSpecs_.size(); ++i) {
        if (i) blob += ',';
        blob += childSpecs_[i];
    }
    blob += "]}";
    return blob;
}

// ---------------------------------------------------------------------------
// Vtable fan-out
// ---------------------------------------------------------------------------

void CompositeScript::init(ScriptContext& ctx, ecs::Entity self) {
    forEachChild([&](Script& s) { s.init(ctx, self); });
}

void CompositeScript::update(ScriptContext& ctx, ecs::Entity self, float dt) {
    forEachChild([&](Script& s) { s.update(ctx, self, dt); });
}

void CompositeScript::onUnload(ScriptContext& ctx, ecs::Entity self) {
    // Reverse of init order — teardown mirrors construction so a later behavior
    // that depended on an earlier one during init unwinds first.
    forEachChildReverse([&](Script& s) { s.onUnload(ctx, self); });
}

void CompositeScript::onScroll(ScriptContext& ctx, ecs::Entity self, double x, double y) {
    forEachChild([&](Script& s) { s.onScroll(ctx, self, x, y); });
}

void CompositeScript::onCollisionEnter(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                                       const physics::ContactInfo& contact) {
    forEachChild([&](Script& s) { s.onCollisionEnter(ctx, self, other, contact); });
}

void CompositeScript::onCollisionExit(ScriptContext& ctx, ecs::Entity self, ecs::Entity other,
                                      const physics::ContactInfo& contact) {
    forEachChild([&](Script& s) { s.onCollisionExit(ctx, self, other, contact); });
}

void CompositeScript::onUIPress  (ScriptContext& ctx, ecs::Entity self) { forEachChild([&](Script& s) { s.onUIPress(ctx, self); }); }
void CompositeScript::onUIRelease(ScriptContext& ctx, ecs::Entity self) { forEachChild([&](Script& s) { s.onUIRelease(ctx, self); }); }
void CompositeScript::onUIEnter  (ScriptContext& ctx, ecs::Entity self) { forEachChild([&](Script& s) { s.onUIEnter(ctx, self); }); }
void CompositeScript::onUILeave  (ScriptContext& ctx, ecs::Entity self) { forEachChild([&](Script& s) { s.onUILeave(ctx, self); }); }

void CompositeScript::onTextInput(ScriptContext& ctx, ecs::Entity self, unsigned int codepoint) {
    forEachChild([&](Script& s) { s.onTextInput(ctx, self, codepoint); });
}

void CompositeScript::drawInspector(EditorContext& ctx) {
    forEachChild([&](Script& s) { s.drawInspector(ctx); });
}

// ---------------------------------------------------------------------------
// get_behavior resolution — by Python class name
// ---------------------------------------------------------------------------

void* CompositeScript::pyHandleForClass(const std::string& cls) {
    for (auto& ch : children_) {
        if (ch && ch->behaviorClassName() == cls) return ch->pyInstanceHandle();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Save-game state — one JSON object keyed by child class name
//   { "Health": {...}, "Burnable": {...} }
// Children without a save_state() hook contribute nothing; an all-empty result
// yields "" so the entity's save node carries no scriptState at all.
// ---------------------------------------------------------------------------

std::string CompositeScript::serializeState() const {
    JsonWriter w;
    w.beginObject();
    bool any = false;
    for (auto& ch : children_) {
        if (!ch) continue;
        std::string cls = ch->behaviorClassName();
        if (cls.empty()) continue;
        std::string s = ch->serializeState();
        if (s.empty()) continue;
        w.writeRawValue(cls.c_str(), s.c_str());   // s is already a JSON object
        any = true;
    }
    w.endObject();
    return any ? w.str() : std::string{};
}

void CompositeScript::deserializeState(const std::string& json) {
    if (json.empty()) return;
    JsonNode root;
    try { root = parseJson(json.c_str()); }
    catch (...) {
        std::fprintf(stderr, "CompositeScript: scriptState is not valid JSON -- ignored\n");
        return;
    }
    if (!root.isObject()) return;
    for (auto& ch : children_) {
        if (!ch) continue;
        std::string cls = ch->behaviorClassName();
        if (cls.empty() || !root.contains(cls)) continue;
        ch->deserializeState(dumpJson(root[cls]));
    }
}
