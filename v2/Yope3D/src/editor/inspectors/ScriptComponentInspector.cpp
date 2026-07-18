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
// Per-param value storage. sBuf / listBuf are char arrays owned by each entry
// so multiple str/strlist params never share a buffer.
// ---------------------------------------------------------------------------
struct LiveParamValue {
    float                    f       = 0.f;
    int                      i       = 0;
    bool                     b       = false;
    std::string              s;
    std::vector<std::string> list;
    char                     sBuf[256]  = {};  // InputText staging for "str"/"enum"
    char                     listBuf[2048] = {}; // InputTextMultiline staging for "strlist"
};

// ---------------------------------------------------------------------------
// Inspector-local state (valid for one entity at a time)
// ---------------------------------------------------------------------------
static ecs::Entity                           s_lastEntity   = ecs::NullEntity;
static char                                  s_lastBlob[2048] = {};
static std::string                           s_selectedClass;
static std::map<std::string, LiveParamValue> s_paramValues;
static char                                  s_filterBuf[64] = {};

// ---------------------------------------------------------------------------
// JSON round-trip helpers
// ---------------------------------------------------------------------------
static std::string buildBlob(const BehaviorEntry* entry) {
    JsonWriter w;
    w.beginObject();
    w.writeString("module", entry ? entry->modulePath.c_str() : "");
    w.writeString("class",  s_selectedClass.c_str());
    if (entry) {
        for (const auto& pd : entry->params) {
            auto it = s_paramValues.find(pd.name);
            if (it == s_paramValues.end()) continue;
            const LiveParamValue& v = it->second;
            if (pd.type == "float")        w.writeFloat (pd.name.c_str(), v.f);
            else if (pd.type == "int")     w.writeInt   (pd.name.c_str(), v.i);
            else if (pd.type == "bool")    w.writeBool  (pd.name.c_str(), v.b);
            else if (pd.type == "strlist") {
                w.beginArray(pd.name.c_str());
                for (const auto& item : v.list) w.writeArrayString(item.c_str());
                w.endArray();
            } else {
                w.writeString(pd.name.c_str(), v.s.c_str());
            }
        }
    }
    w.endObject();
    return w.str();
}

static void loadParamValues(const char* blob, const BehaviorEntry* entry) {
    s_paramValues.clear();
    if (!entry) return;
    for (const auto& pd : entry->params) {
        LiveParamValue v;
        if      (pd.type == "float")   v.f    = pd.fDefault;
        else if (pd.type == "int")     v.i    = pd.iDefault;
        else if (pd.type == "bool")    v.b    = pd.bDefault;
        else if (pd.type == "strlist") v.list = pd.listDefault;
        else                           v.s    = pd.sDefault;
        s_paramValues[pd.name] = v;
    }
    try {
        JsonNode root = parseJson(blob);
        if (!root.isObject()) return;
        for (const auto& pd : entry->params) {
            if (!root.contains(pd.name)) continue;
            const JsonNode& n = root[pd.name];
            LiveParamValue& v = s_paramValues[pd.name];
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
    } catch (...) {}
    // Sync char buffers from parsed string values
    for (auto& [name, v] : s_paramValues) {
        std::strncpy(v.sBuf, v.s.c_str(), sizeof(v.sBuf) - 1);
        // Build listBuf: one path per line
        std::string flat;
        for (const auto& item : v.list) { flat += item; flat += '\n'; }
        std::strncpy(v.listBuf, flat.c_str(), sizeof(v.listBuf) - 1);
    }
}

static void syncFromComponent(const ecs::ScriptComponent* sc) {
    s_selectedClass.clear();
    try {
        JsonNode root = parseJson(sc->paramsBlob);
        if (root.contains("class")) s_selectedClass = root["class"].asString();
    } catch (...) {}
    loadParamValues(sc->paramsBlob, BehaviorRegistry::findByClass(s_selectedClass));
}

static void flushToComponent(ecs::ScriptComponent* sc) {
    const BehaviorEntry* entry = BehaviorRegistry::findByClass(s_selectedClass);
    std::string blob = buildBlob(entry);
    std::strncpy(sc->paramsBlob,  blob.c_str(),  sizeof(sc->paramsBlob)  - 1);
    std::strncpy(sc->scriptClass, "PythonScript", sizeof(sc->scriptClass) - 1);
    std::strncpy(s_lastBlob,      blob.c_str(),  sizeof(s_lastBlob)      - 1);
}

// ---------------------------------------------------------------------------
// Widget drawing — returns {changed, activated, deactivatedAfterEdit}
// ---------------------------------------------------------------------------
struct WidgetResult { bool changed; bool activated; bool deactivatedAfterEdit; };

static WidgetResult drawParamWidget(const ParamDef& pd) {
    WidgetResult r{};
    LiveParamValue& v = s_paramValues[pd.name];
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
    // Re-sync when entity changes OR when paramsBlob was modified externally (undo/redo)
    bool entityChanged = (e.id != s_lastEntity.id || e.generation != s_lastEntity.generation);
    bool blobChanged   = (std::strcmp(sc->paramsBlob, s_lastBlob) != 0);
    if (entityChanged || blobChanged) {
        s_lastEntity = e;
        std::strncpy(s_lastBlob, sc->paramsBlob, sizeof(s_lastBlob) - 1);
        std::memset(s_filterBuf, 0, sizeof(s_filterBuf));
        syncFromComponent(sc);
    }

    static ecs::ScriptComponent beforeEdit{};

    // --- Type badge ---
    ImGui::TextDisabled("Type:");
    ImGui::SameLine();
    ImGui::TextUnformatted("Python");
    ImGui::Spacing();

    // --- Search filter ---
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##filter", s_filterBuf, sizeof(s_filterBuf));
    ImGui::SameLine(0, 4); ImGui::TextDisabled("Search");

    // --- Behavior list ---
    {
        ImGui::BeginChild("##behlist", ImVec2(-1, 90.f), true);
        for (const auto& entry : BehaviorRegistry::all()) {
            if (s_filterBuf[0] != '\0') {
                std::string lower = entry.displayName;
                std::string flt   = s_filterBuf;
                for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
                for (auto& c : flt)   c = (char)std::tolower((unsigned char)c);
                if (lower.find(flt) == std::string::npos) continue;
            }
            bool selected = (entry.className == s_selectedClass);
            if (ImGui::Selectable(entry.displayName.c_str(), selected)) {
                if (!selected) {
                    beforeEdit      = *sc;
                    s_selectedClass = entry.className;
                    loadParamValues("{}", &entry);
                    flushToComponent(sc);
                    ctx.history->execute(ctx,
                        std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                            e, beforeEdit, *sc, "Select Behavior Script"));
                }
            }
        }
        ImGui::EndChild();
    }

    // --- Per-param widgets ---
    const BehaviorEntry* entry = BehaviorRegistry::findByClass(s_selectedClass);
    if (entry && !entry->params.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        for (const auto& pd : entry->params) {
            WidgetResult r = drawParamWidget(pd);
            if (r.activated) beforeEdit = *sc;

            if (r.changed) {
                flushToComponent(sc);
                bool isDrag = (pd.type == "float" || pd.type == "int");
                if (!isDrag || r.deactivatedAfterEdit) {
                    ctx.history->execute(ctx,
                        std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                            e, beforeEdit, *sc, "Edit " + pd.label));
                }
            } else if (r.deactivatedAfterEdit) {
                ctx.history->execute(ctx,
                    std::make_unique<SetComponentCommand<ecs::ScriptComponent>>(
                        e, beforeEdit, *sc, "Edit " + pd.label));
            }
        }
    } else if (!s_selectedClass.empty() && !entry) {
        ImGui::Spacing();
        ImGui::TextDisabled("(behavior not found — check scripts/behaviors/)");
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
