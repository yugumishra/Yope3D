#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "ecs/Entity.h"
#include "world/Transform.h"
#include "world/World.h"
#include "scripting/python/PyComponentTable.h"

namespace py = pybind11;

void bind_ecs(py::module_& m) {
    // Entity handle
    py::class_<ecs::Entity>(m, "Entity")
        .def_readonly("id",         &ecs::Entity::id)
        .def_readonly("generation", &ecs::Entity::generation)
        .def("__eq__", [](const ecs::Entity& a, const ecs::Entity& b) {
            return a.id == b.id && a.generation == b.generation;
        })
        .def("__hash__", [](const ecs::Entity& e) {
            return std::hash<uint64_t>{}((uint64_t)e.id << 32 | e.generation);
        })
        .def("__repr__", [](const ecs::Entity& e) {
            return "Entity(id=" + std::to_string(e.id) + ", gen=" + std::to_string(e.generation) + ")";
        });

    // Transform component
    py::class_<Transform>(m, "Transform")
        .def_readwrite("position", &Transform::position)
        .def_readwrite("rotation", &Transform::rotation)
        .def_readwrite("scale",    &Transform::scale);

    // Hull (rigid body state) — expose commonly scripted fields
    py::class_<ecs::Hull>(m, "Hull")
        .def_readwrite("velocity",        &ecs::Hull::velocity)
        .def_readwrite("omega",           &ecs::Hull::omega)
        .def_readwrite("mass",            &ecs::Hull::mass)
        .def_readwrite("linear_damping",  &ecs::Hull::linearDamping)
        .def_readwrite("angular_damping", &ecs::Hull::angularDamping)
        .def_readwrite("friction",        &ecs::Hull::friction)
        .def_readwrite("restitution",     &ecs::Hull::restitution)
        .def_readwrite("gravity",         &ecs::Hull::gravity)
        .def_readwrite("tangible",        &ecs::Hull::tangible);

    // Shape forms
    py::class_<ecs::SphereForm>(m, "SphereForm")
        .def_readwrite("radius", &ecs::SphereForm::radius);

    py::class_<ecs::AABBForm>(m, "AABBForm")
        .def_readwrite("extent", &ecs::AABBForm::extent);

    py::class_<ecs::OBBForm>(m, "OBBForm")
        .def_readwrite("extent", &ecs::OBBForm::extent);

    py::class_<ecs::CapsuleForm>(m, "CapsuleForm")
        .def_readwrite("radius",      &ecs::CapsuleForm::radius)
        .def_readwrite("half_height", &ecs::CapsuleForm::halfHeight);

    py::class_<ecs::CylinderForm>(m, "CylinderForm")
        .def_readwrite("radius",      &ecs::CylinderForm::radius)
        .def_readwrite("half_height", &ecs::CylinderForm::halfHeight);

    // LightSource — float arrays exposed as Vec3 via lambdas
    py::class_<ecs::LightSource>(m, "LightSource")
        .def_readwrite("type",       &ecs::LightSource::type)
        .def_readwrite("intensity",  &ecs::LightSource::intensity)
        .def_property("color",
            [](const ecs::LightSource& l) { return math::Vec3{l.color[0], l.color[1], l.color[2]}; },
            [](ecs::LightSource& l, math::Vec3 v) { l.color[0]=v.x; l.color[1]=v.y; l.color[2]=v.z; })
        .def_property("position",
            [](const ecs::LightSource& l) { return math::Vec3{l.position[0], l.position[1], l.position[2]}; },
            [](ecs::LightSource& l, math::Vec3 v) { l.position[0]=v.x; l.position[1]=v.y; l.position[2]=v.z; })
        .def_property("direction",
            [](const ecs::LightSource& l) { return math::Vec3{l.direction[0], l.direction[1], l.direction[2]}; },
            [](ecs::LightSource& l, math::Vec3 v) { l.direction[0]=v.x; l.direction[1]=v.y; l.direction[2]=v.z; });

    // Name — expose as str-like access
    py::class_<ecs::Name>(m, "Name")
        .def_property("value",
            [](const ecs::Name& n) { return std::string(n.value); },
            [](ecs::Name& n, const std::string& s) {
                std::strncpy(n.value, s.c_str(), sizeof(n.value) - 1);
                n.value[sizeof(n.value) - 1] = '\0';
            })
        .def("__repr__", [](const ecs::Name& n) { return "Name(\"" + std::string(n.value) + "\")"; });

    // SpringConstraint
    py::class_<ecs::SpringConstraint>(m, "SpringConstraint")
        .def_readwrite("target",      &ecs::SpringConstraint::target)
        .def_readwrite("k",           &ecs::SpringConstraint::k)
        .def_readwrite("rest_length", &ecs::SpringConstraint::restLength);

    // ScriptComponent — expose class name and params
    py::class_<ecs::ScriptComponent>(m, "ScriptComponent")
        .def_property("script_class",
            [](const ecs::ScriptComponent& s) { return std::string(s.scriptClass); },
            [](ecs::ScriptComponent& s, const std::string& v) {
                std::strncpy(s.scriptClass, v.c_str(), sizeof(s.scriptClass) - 1);
            })
        .def_property("params_blob",
            [](const ecs::ScriptComponent& s) { return std::string(s.paramsBlob); },
            [](ecs::ScriptComponent& s, const std::string& v) {
                std::strncpy(s.paramsBlob, v.c_str(), sizeof(s.paramsBlob) - 1);
            });

    // view(*component_names) → list of tuples (entity, comp1, comp2, ...)
    // The registry is accessed via the module-level 'yope.world' attribute.
    // Note: must be called from the main thread while physics is paused.
    m.def("view", [](py::args names) -> py::list {
        auto yope = py::module_::import("yope");
        auto worldObj = yope.attr("world");
        if (worldObj.is_none()) {
            throw std::runtime_error("yope.world not bound — call bindContext first");
        }
        auto* world = worldObj.cast<World*>();
        auto& reg = world->getRegistry();

        // Collect TypeIds for the requested component names
        std::vector<std::string> nameVec;
        std::vector<ecs::TypeId> typeIds;
        for (auto& n : names) {
            std::string s = n.cast<std::string>();
            ecs::TypeId tid = PyComponentTable::typeIdForName(s);
            if (tid == static_cast<ecs::TypeId>(-1)) {
                throw std::runtime_error("Unknown component: " + s);
            }
            nameVec.push_back(s);
            typeIds.push_back(tid);
        }

        // Sort TypeIds (required is a superset check; entitiesWith expects sorted)
        auto sortedIds = typeIds;
        std::sort(sortedIds.begin(), sortedIds.end());

        auto entities = reg.entitiesWith(sortedIds);

        py::list result;
        for (auto e : entities) {
            py::tuple row(1 + nameVec.size());
            row[0] = py::cast(e);
            for (size_t i = 0; i < nameVec.size(); ++i) {
                void* ptr = reg.getRaw(e, typeIds[i]);
                row[i + 1] = PyComponentTable::wrapPtr(nameVec[i], ptr);
            }
            result.append(row);
        }
        return result;
    }, "Returns a list of (entity, comp...) tuples for entities with all named components.");

    // reg_get / reg_has helpers
    m.def("reg_get", [](ecs::Entity e, const std::string& name) -> py::object {
        auto* world = py::module_::import("yope").attr("world").cast<World*>();
        void* ptr = world->getRegistry().getRaw(e, PyComponentTable::typeIdForName(name));
        return PyComponentTable::wrapPtr(name, ptr);
    });
    m.def("reg_has", [](ecs::Entity e, const std::string& name) -> bool {
        auto* world = py::module_::import("yope").attr("world").cast<World*>();
        auto tid = PyComponentTable::typeIdForName(name);
        return world->getRegistry().getRaw(e, tid) != nullptr;
    });
    m.def("reg_valid", [](ecs::Entity e) -> bool {
        auto* world = py::module_::import("yope").attr("world").cast<World*>();
        return world->getRegistry().valid(e);
    });
}
#endif // YOPE_PYTHON
