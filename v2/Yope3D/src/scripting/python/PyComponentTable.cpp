#ifdef YOPE_PYTHON
#include "scripting/python/PyComponentTable.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "world/Transform.h"

namespace py = pybind11;

namespace PyComponentTable {

static std::vector<PyCompEntry> s_entries;

// Build a full table entry for component type T: name + typeId + py wrapper +
// default-add + remove. Keeps build() a flat, declarative list.
template <class T>
static PyCompEntry entryFor(const char* name) {
    return {
        name,
        ecs::typeId<T>(),
        [](void* p) { return py::cast(static_cast<T*>(p), py::return_value_policy::reference); },
        [](ecs::Registry& r, ecs::Entity e) { if (!r.has<T>(e)) r.add<T>(e, T{}); },
        [](ecs::Registry& r, ecs::Entity e) { if (r.has<T>(e))  r.remove<T>(e); }
    };
}

// Zero-size tag components (Sleeping/Fixed) aren't bound py::class_es, so their wrap
// just returns True (presence) — reg_get(e, "Fixed") is True/None; reg_has is the idiom.
template <class T>
static PyCompEntry tagEntryFor(const char* name) {
    return {
        name,
        ecs::typeId<T>(),
        [](void*) { return py::object(py::cast(true)); },
        [](ecs::Registry& r, ecs::Entity e) { if (!r.has<T>(e)) r.add<T>(e, T{}); },
        [](ecs::Registry& r, ecs::Entity e) { if (r.has<T>(e))  r.remove<T>(e); }
    };
}

void build() {
    s_entries = {
        entryFor<Transform>             ("Transform"),
        entryFor<ecs::Hull>             ("Hull"),
        entryFor<ecs::SphereForm>       ("SphereForm"),
        entryFor<ecs::AABBForm>         ("AABBForm"),
        entryFor<ecs::OBBForm>          ("OBBForm"),
        entryFor<ecs::CapsuleForm>      ("CapsuleForm"),
        entryFor<ecs::CylinderForm>     ("CylinderForm"),
        entryFor<ecs::CompoundCollider> ("CompoundCollider"),
        entryFor<ecs::Material>         ("Material"),
        entryFor<ecs::LightSource>      ("LightSource"),
        entryFor<ecs::Name>             ("Name"),
        entryFor<ecs::SpringConstraint> ("SpringConstraint"),
        entryFor<ecs::PointJointConstraint>    ("PointJointConstraint"),
        entryFor<ecs::HingeJointConstraint>    ("HingeJointConstraint"),
        entryFor<ecs::ConeTwistJointConstraint>("ConeTwistJointConstraint"),
        entryFor<ecs::Parent>           ("Parent"),
        entryFor<ecs::ScriptComponent>  ("ScriptComponent"),
        entryFor<ecs::UITransform>      ("UITransform"),
        entryFor<ecs::UIBackground>     ("UIBackground"),
        entryFor<ecs::UITexturedBackground>("UITexturedBackground"),
        entryFor<ecs::UIText>           ("UIText"),
        entryFor<ecs::UIButton>        ("UIButton"),
        entryFor<ecs::TextLabel3D>      ("TextLabel3D"),
        entryFor<ecs::AudioSource>      ("AudioSource"),
        tagEntryFor<ecs::Sleeping>      ("Sleeping"),
        tagEntryFor<ecs::Fixed>         ("Fixed"),
    };
}

const std::vector<PyCompEntry>& entries() { return s_entries; }

ecs::TypeId typeIdForName(const std::string& name) {
    for (auto& e : s_entries) {
        if (e.name == name) return e.typeId;
    }
    return static_cast<ecs::TypeId>(-1);
}

py::object wrapPtr(const std::string& name, void* ptr) {
    if (!ptr) return py::none();
    for (auto& e : s_entries) {
        if (e.name == name) return e.wrap(ptr);
    }
    return py::none();
}

bool addByName(ecs::Registry& reg, ecs::Entity e, const std::string& name) {
    for (auto& en : s_entries) {
        if (en.name == name) { en.addDefault(reg, e); return true; }
    }
    return false;
}

bool removeByName(ecs::Registry& reg, ecs::Entity e, const std::string& name) {
    for (auto& en : s_entries) {
        if (en.name == name) { en.remove(reg, e); return true; }
    }
    return false;
}

} // namespace PyComponentTable
#endif // YOPE_PYTHON
