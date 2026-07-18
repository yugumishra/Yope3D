#include "editor/inspectors/InspectorRegistry.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "scripting/Script.h"
#include "scene/serialization/JsonParser.h"
#include "scene/serialization/JsonWriter.h"
#include <imgui.h>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#ifdef YOPE_PYTHON
#include "scripting/python/BehaviorRegistry.h"

// ---------------------------------------------------------------------------
// The inspector edits a *stack* of behaviors (composite-behaviors.md). One
// behavior stores as a plain PythonScript; two or more materialize a
// CompositeScript. The developer never sees that machinery — they see a list of
// typed behavior blocks with add/remove/reorder.
// ---------------------------------------------------------------------------

// Per-param value storage. sBuf / listBuf are char arrays owned by each entry so
// multiple str/strlist params never share a buffer.
struct LiveParamValue {
    float                    f       = 0.f;
    int                      i       = 0;
    bool                     b       = false;
    std::string              s;
    std::vector<std::string> list;
    char                     sBuf[256]     = {};  // InputText staging for "str"/"enum"
    char                     listBuf[2048] = {};  // InputTextMultiline staging for "strlist"
};

// One behavior in the stack: its class + module + live param values.
struct StackEntry {
    std::string                          className;
    std::string                          modulePath;
    std::map<std::string, LiveParamValue> values;
};

// ---------------------------------------------------------------------------
// Inspector-local state (valid for one entity at a time)
// ---------------------------------------------------------------------------
static ecs::Entity              s_lastEntity     = ecs::NullEntity;
static char                     s_lastBlob[2048] = {};
static std::vector<StackEntry>  s_stack;
static bool                     s_pickerOpen     = false;
static char                     s_filterBuf[64]  = {};

// ---------------------------------------------------------------------------
// Value <-> JSON helpers
// ---------------------------------------------------------------------------
static std::map<std::string, LiveParamValue> loadValues(const JsonNode& paramsObj,
                                                         const BehaviorEntry* entry) {
    std::map<std::string, LiveParamValue> out;
    if (!entry) return out;
    for (const auto& pd : entry->params) {
        LiveParamValue v;
        if      (pd.type == "float")   v.f    = pd.fDefault;
        else if (pd.type == "int")     v.i    = pd.iDefault;
        else if (pd.type == "bool")    v.b    = pd.bDefault;
        else if (pd.type == "strlist") v.list = pd.listDefault;
        else                           v.s    = pd.sDefault;
        out[pd.name] = v;
    }
    if (paramsObj.isObject()) {
        for (const auto& pd : entry->params) {
            if (!paramsObj.contains(pd.name)) continue;
            const JsonNode& n = paramsObj[pd.name];
            LiveParamValue& v = out[pd.name];
            if      (pd.type == "float"   && n.isNumber()) v.f = n.asFloat();
            else if (pd.type == "int"     && n.isNumber()) v.i = n.asInt();
            else if (pd.type == "bool"    && n.isBool())   v.b = n.asBool();
            else if (pd.type == "strlist" && n.isArray()) {
                v.list.clear();
                for (const auto& item : n.asArray())
                    if (item.isString()) v.list.push_back(item.asString());
            } else if (n.isString()) {
                v.s = n.asString();
            }
        }
    }
    // Sync char buffers from parsed string/list values.
    for (auto& [name, v] : out) {
        std::strncpy(v.sBuf, v.s.c_str(), sizeof(v.sBuf) - 1);
        std::string flat;
        for (const auto& item : v.list) { flat += item; flat += '\n'; }
        std::strncpy(v.listBuf, flat.c_str(), sizeof(v.listBuf) - 1);
    }
    return out;
}

// Write one behavior's param values as JSON keys at the writer's current object
// level (used flat for a single script, and inside "params" for a composite).
static void writeParamValues(JsonWriter& w, const BehaviorEntry* entry,
                             const std::map<std::string, LiveParamValue>& values) {
    if (!entry) return;
    for (const auto& pd : entry->params) {
        auto it = values.find(pd.name);
        if (it == values.end()) continue;
        const LiveParamValue& v = it->second;
        if      (pd.type == "float") w.writeFloat(pd.name.c_str(), v.f);
        else if (pd.type == "int")   w.writeInt  (pd.name.c_str(), v.i);
        else if (pd.type == "bool")  w.writeBool (pd.name.c_str(), v.b);
        else if (pd.type == "strlist") {
            w.beginArray(pd.name.c_str());
            for (const auto& item : v.list) w.writeArrayString(item.c_str());
            w.endArray();
        } else {
            w.writeString(pd.name.c_str(), v.s.c_str());
        }
    }
}

// Rebuild the whole stack into ScriptComponent (scriptClass + paramsBlob). One
// entry -> a flat PythonScript blob; 2+ -> a CompositeScript "scripts" blob.
static void flushStackToComponent(ecs::ScriptComponent* sc) {
    std::string blob;
    const char* cls = "";
    if (s_stack.size() == 1) {
        cls = "PythonScript";
        const StackEntry& se = s_stack[0];
        JsonWriter w;
        w.beginObject();
        w.writeString("module", se.modulePath.c_str());
        w.writeString("class",  se.className.c_str());
        writeParamValues(w, BehaviorRegistry::findByClass(se.className), se.values);
        w.endObject();
        blob = w.str();
    } else if (s_stack.size() >= 2) {
        cls = "CompositeScript";
        JsonWriter w;
        w.beginObject();
        w.beginArray("scripts");
        for (const auto& se : s_stack) {
            w.beginArrayObject();
            w.writeString("module", se.modulePath.c_str());
            w.writeString("class",  se.className.c_str());
            w.writeKey("params");
            w.beginObject();
            writeParamValues(w, BehaviorRegistry::findByClass(se.className), se.values);
            w.endObject();
            w.endObject();
        }
        w.endArray();
        w.endObject();
        blob = w.str();
    } else {
        blob = "{}";
    }

    std::strncpy(sc->scriptClass, cls,          sizeof(sc->scriptClass) - 1);
    sc->scriptClass[sizeof(sc->scriptClass) - 1] = '\0';
    if (blob.size() < sizeof(sc->paramsBlob)) {
        std::strncpy(sc->paramsBlob, blob.c_str(), sizeof(sc->paramsBlob) - 1);
        sc->paramsBlob[sizeof(sc->paramsBlob) - 1] = '\0';
    }
    std::strncpy(s_lastBlob, sc->paramsBlob, sizeof(s_lastBlob) - 1);
}

// Parse the entity's ScriptComponent into the editable stack.
static void syncStackFromComponent(const ecs::ScriptComponent* sc) {
    s_stack.clear();
    if (sc->scriptClass[0] == '\0') return;
    try {
        JsonNode root = parseJson(sc->paramsBlob);
        if (std::strcmp(sc->scriptClass, "CompositeScript") == 0) {
            if (root.contains("scripts") && root["scripts"].isArray()) {
                for (const auto& elem : root["scripts"].asArray()) {
                    StackEntry se;
                    se.className  = elem.contains("class")  ? elem["class"].asString()  : "";
                    se.modulePath = elem.contains("module") ? elem["module"].asString() : "";
                    const JsonNode& params = elem.contains("params") ? elem["params"] : JsonNode::nullNode();
                    se.values = loadValues(params, BehaviorRegistry::findByClass(se.className));
                    s_stack.push_back(std::move(se));
                }
            }
        } else {
            // Single script: flat blob, params are siblings of module/class.
            StackEntry se;
            se.className  = root.contains("class")  ? root["class"].asString()  : "";
            se.modulePath = root.contains("module") ? root["module"].asString() : "";
            se.values = loadValues(root, BehaviorRegistry::findByClass(se.className));
            s_stack.push_back(std::move(se));
        }
    } catch (...) {}
}

// ---------------------------------------------------------------------------
// Widget drawing — returns {changed, activated, deactivatedAfterEdit}
// ---------------------------------------------------------------------------
struct WidgetResult { bool changed; bool activated; bool deactivatedAfterEdit; };

static WidgetResult drawParamWidget(const ParamDef& pd, LiveParamValue& v) {
    WidgetResult r{};
    ImGui::PushID(pd.name.c_str());
    const float labelW = ImGui::GetContentRegionAvail().x * 0.48f;

    if (pd.type == "float") {
        ImGui::TextUnformatted(pd.label.c_str());
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-1);
        r.changed              = ImGui::DragFloat("##v", &v.f, 0.01f);
        r.activated            = ImGui::IsItemActivated();
        r.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    } else if (pd.type == "int") {
        ImGui::TextUnformatted(pd.label.c_str());
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-1);
        r.changed              = ImGui::DragInt("##v", &v.i);
        r.activated            = ImGui::IsItemActivated();
        r.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    } else if (pd.type == "bool") {
        r.changed              = ImGui::Checkbox(pd.label.c_str(), &v.b);
        r.activated            = r.changed;
        r.deactivatedAfterEdit = r.changed;
    } else if (pd.type == "enum") {
        ImGui::TextUnformatted(pd.label.c_str());
        for (const auto& opt : pd.options) {
            ImGui::SameLine();
            bool sel = (v.s == opt);
            if (ImGui::RadioButton(opt.c_str(), sel) && !sel) {
                v.s = opt;
                r.changed = r.activated = r.deactivatedAfterEdit = true;
            }
        }
    } else if (pd.type == "strlist") {
        ImGui::TextUnformatted(pd.label.c_str());
        if (ImGui::InputTextMultiline("##strlist", v.listBuf, sizeof(v.listBuf), ImVec2(-1, 64))) {
            v.list.clear();
            std::string line;
            for (const char* p = v.listBuf; *p; ++p) {
                if (*p == '\n') { if (!line.empty()) { v.list.push_back(line); line.clear(); } }
                else line += *p;
            }
            if (!line.empty()) v.list.push_back(line);
            r.changed = true;
        }
        r.activated            = ImGui::IsItemActivated();
        r.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    } else {
        // "str" — sBuf persists per param so no cross-widget aliasing
        ImGui::TextUnformatted(pd.label.c_str());
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##v", v.sBuf, sizeof(v.sBuf))) {
            v.s = v.sBuf;
            r.changed = true;
        }
        r.activated            = ImGui::IsItemActivated();
        r.deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    }

    ImGui::PopID();
    return r;
}

// True if a behavior class is already somewhere in the stack.
static bool stackHasClass(const std::string& cls) {
    for (const auto& se : s_stack) if (se.className == cls) return true;
    return false;
}
#endif // YOPE_PYTHON

// ---------------------------------------------------------------------------
// Main inspector entry point
// ---------------------------------------------------------------------------
void drawScriptComponent(void* comp, EditorContext& ctx, ecs::Entity e) {
    auto* sc = static_cast<ecs::ScriptComponent*>(comp);

    bool removeRequested = false;
    if (!componentHeader("Script Component", "X##screm", &removeRequested)) {
        if (removeRequested && ctx.registry) ctx.registry->remove<ecs::ScriptComponent>(e);
        return;
    }
    if (removeRequested && ctx.registry) { ctx.registry->remove<ecs::ScriptComponent>(e); return; }

#ifdef YOPE_PYTHON
    // Re-sync when the entity changes OR the blob was modified externally (undo/redo).
    bool entityChanged = (e.id != s_lastEntity.id || e.generation != s_lastEntity.generation);
    bool blobChanged   = (std::strcmp(sc->paramsBlob, s_lastBlob) != 0);
    if (entityChanged || blobChanged) {
        s_lastEntity = e;
        std::strncpy(s_lastBlob, sc->paramsBlob, sizeof(s_lastBlob) - 1);
        std::memset(s_filterBuf, 0, sizeof(s_filterBuf));
        s_pickerOpen = false;
        syncStackFromComponent(sc);
    }

    static ecs::ScriptComponent beforeEdit{};

    // Deferred structural edits (applied after the render loop so we never mutate
    // s_stack while iterating it): remove index, or move index by delta.
    int removeIdx = -1;
    int moveIdx = -1, moveDelta = 0;

    // --- Behavior blocks ---
    for (size_t i = 0; i < s_stack.size(); ++i) {
        StackEntry& se = s_stack[i];
        ImGui::PushID(static_cast<int>(i));

        const BehaviorEntry* entry = BehaviorRegistry::findByClass(se.className);
        std::string title = se.className.empty() ? "(unset)" : se.className;
        bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Reorder / remove controls on the header line.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 66.f);
        if (ImGui::SmallButton("^") && i > 0)              { moveIdx = (int)i; moveDelta = -1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < s_stack.size()) { moveIdx = (int)i; moveDelta = +1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))                        { removeIdx = (int)i; }

        if (open) {
            if (!entry) {
                ImGui::TextDisabled("(behavior not found — check scripts/behaviors/)");
            } else if (entry->params.empty()) {
                ImGui::TextDisabled("(no parameters)");
            } else {
                for (const auto& pd : entry->params) {
                    LiveParamValue& v = se.values[pd.name];
                    WidgetResult r = drawParamWidget(pd, v);
                    if (r.activated) beforeEdit = *sc;
                    bool isDrag = (pd.type == "float" || pd.type == "int");
                    if (r.changed) {
                        flushStackToComponent(sc);
                        if (!isDrag || r.deactivatedAfterEdit)
                            ctx.history->execute(ctx,
                                std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                                    e, beforeEdit, *sc, "Edit " + pd.label));
                    } else if (r.deactivatedAfterEdit) {
                        ctx.history->execute(ctx,
                            std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                                e, beforeEdit, *sc, "Edit " + pd.label));
                    }
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();

    // --- Add behavior ---
    if (!s_pickerOpen) {
        if (ImGui::Button("+ Add Behavior", ImVec2(-1, 0))) {
            s_pickerOpen = true;
            std::memset(s_filterBuf, 0, sizeof(s_filterBuf));
        }
    } else {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##filter", s_filterBuf, sizeof(s_filterBuf));
        ImGui::SameLine(0, 4); ImGui::TextDisabled("Search");

        ImGui::BeginChild("##behlist", ImVec2(-1, 120.f), true);
        for (const auto& b : BehaviorRegistry::all()) {
            if (s_filterBuf[0] != '\0') {
                std::string lower = b.displayName, flt = s_filterBuf;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                for (auto& c : flt)   c = (char)std::tolower((unsigned char)c);
                if (lower.find(flt) == std::string::npos) continue;
            }
            bool dup = stackHasClass(b.className);
            if (dup) ImGui::BeginDisabled();
            if (ImGui::Selectable(b.displayName.c_str())) {
                beforeEdit = *sc;
                StackEntry se;
                se.className  = b.className;
                se.modulePath = b.modulePath;
                se.values     = loadValues(JsonNode::nullNode(), &b);   // defaults
                s_stack.push_back(std::move(se));
                flushStackToComponent(sc);
                ctx.history->execute(ctx,
                    std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                        e, beforeEdit, *sc, "Add Behavior"));
                s_pickerOpen = false;
            }
            if (dup) {
                ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::TextDisabled("(added)");
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel")) s_pickerOpen = false;
    }

    // --- Apply deferred structural edits ---
    if (removeIdx >= 0 && removeIdx < (int)s_stack.size()) {
        beforeEdit = *sc;
        s_stack.erase(s_stack.begin() + removeIdx);
        flushStackToComponent(sc);
        ctx.history->execute(ctx,
            std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                e, beforeEdit, *sc, "Remove Behavior"));
    } else if (moveIdx >= 0) {
        int j = moveIdx + moveDelta;
        if (j >= 0 && j < (int)s_stack.size()) {
            beforeEdit = *sc;
            std::swap(s_stack[moveIdx], s_stack[j]);
            flushStackToComponent(sc);
            ctx.history->execute(ctx,
                std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                    e, beforeEdit, *sc, "Reorder Behavior"));
        }
    }

    // --- Runtime status ---
    if (sc->instance) {
        ImGui::Spacing();
        sc->instance->drawInspector(ctx);
    }

#else
    // Fallback without Python — raw text fields
    static ecs::ScriptComponent beforeEdit{};
    static char classBuf[64];
    static char paramsBuf[2048];
    std::strncpy(classBuf,  sc->scriptClass, sizeof(classBuf));
    std::strncpy(paramsBuf, sc->paramsBlob,  sizeof(paramsBuf));

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##scriptclass", classBuf, sizeof(classBuf)))
        std::strncpy(sc->scriptClass, classBuf, sizeof(sc->scriptClass) - 1);
    ImGui::SameLine(0, 4); ImGui::TextDisabled("Script Class");
    if (ImGui::IsItemActivated()) beforeEdit = *sc;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
            e, beforeEdit, *sc, "Edit Script Class"));

    ImGui::Text("Params (JSON):");
    if (ImGui::InputTextMultiline("##params", paramsBuf, sizeof(paramsBuf), ImVec2(-1, 80)))
        std::strncpy(sc->paramsBlob, paramsBuf, sizeof(sc->paramsBlob) - 1);
    if (ImGui::IsItemActivated()) beforeEdit = *sc;
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.history->execute(ctx, std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
            e, beforeEdit, *sc, "Edit Script Params"));

    if (sc->instance) { ImGui::Spacing(); sc->instance->drawInspector(ctx); }
#endif
}
#endif // YOPE_EDITOR
