#ifdef YOPE_PYTHON
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include "ecs/Entity.h"
#include "world/Transform.h"
#include "world/TransformHierarchy.h"
#include "world/World.h"
#include "audio/Source.h"   // complete type for AudioSource.source binding
#include "scripting/Script.h"  // Script::pyInstanceHandle for get_behavior
#include "scripting/python/PyComponentTable.h"
#include "scripting/python/PythonInterpreter.h"  // boundContext() for attach_script
#include "scripting/ScriptContext.h"
#include "scene/SceneManager.h"

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
        .def_readwrite("tangible",        &ecs::Hull::tangible)
        .def_readwrite("is_trigger",      &ecs::Hull::isTrigger)
        .def_readwrite("collision_layer", &ecs::Hull::collisionLayer)
        .def_readwrite("collision_mask",  &ecs::Hull::collisionMask);

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

    // CompoundCollider — read-only: the .bcbvh path is authored via the editor's
    // "Generate Static Collider" button, not from scripts. `compiled` (the
    // runtime CompiledCollider*) isn't exposed, only whether it resolved.
    py::class_<ecs::CompoundCollider>(m, "CompoundCollider")
        .def_property_readonly("asset_path",
            [](const ecs::CompoundCollider& c) { return std::string(c.assetPath); })
        .def_property_readonly("loaded",
            [](const ecs::CompoundCollider& c) { return c.compiled != nullptr; });

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
            [](ecs::LightSource& l, math::Vec3 v) { l.direction[0]=v.x; l.direction[1]=v.y; l.direction[2]=v.z; })
        // Attenuation (point/spot) + cone angles in radians (spot/flash). The spot
        // shadow frustum's FOV is derived from outer_cone_angle. Prefer
        // World.set_shadow_caster over toggling casts_shadow directly — it enforces
        // the single-caster (radio) invariant the renderer relies on.
        .def_readwrite("constant",         &ecs::LightSource::constant)
        .def_readwrite("linear",           &ecs::LightSource::linear)
        .def_readwrite("quadratic",        &ecs::LightSource::quadratic)
        .def_readwrite("inner_cone_angle", &ecs::LightSource::innerConeAngle)
        .def_readwrite("outer_cone_angle", &ecs::LightSource::outerConeAngle)
        .def_readwrite("casts_shadow",     &ecs::LightSource::castsShadow);

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

    // Parent — transform hierarchy link. Transform is LOCAL to parent's frame;
    // use get_world_position() for the composed world position.
    py::class_<ecs::Parent>(m, "Parent")
        .def_readwrite("parent", &ecs::Parent::parent);

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

    // ---- UI + 3D-text components (mutate after creation via reg_get / set_text) ----
    py::class_<ecs::UITransform>(m, "UITransform")
        .def_readwrite("min_x", &ecs::UITransform::minX)
        .def_readwrite("min_y", &ecs::UITransform::minY)
        .def_readwrite("max_x", &ecs::UITransform::maxX)
        .def_readwrite("max_y", &ecs::UITransform::maxY)
        .def_readwrite("depth", &ecs::UITransform::depth)
        .def_readwrite("visible", &ecs::UITransform::visible)
        // Anchor: 0=Free (legacy) 1=TopLeft 2=TopRight 3=BottomLeft 4=BottomRight 5=Center.
        // SizeMode (only consulted when anchor != Free): 0=Fraction 1=Pixel.
        .def_readwrite("anchor",       &ecs::UITransform::anchor)
        .def_readwrite("size_mode",    &ecs::UITransform::sizeMode)
        .def_readwrite("pixel_width",  &ecs::UITransform::pixelWidth)
        .def_readwrite("pixel_height", &ecs::UITransform::pixelHeight)
        .def_readwrite("offset_x_px",  &ecs::UITransform::offsetXPx)
        .def_readwrite("offset_y_px",  &ecs::UITransform::offsetYPx)
        // Own opacity multiplier; composes down an ecs::Parent chain (set_ui_parent).
        .def_readwrite("opacity", &ecs::UITransform::opacity);

    py::class_<ecs::UIBackground>(m, "UIBackground")
        .def_readwrite("r", &ecs::UIBackground::r)
        .def_readwrite("g", &ecs::UIBackground::g)
        .def_readwrite("b", &ecs::UIBackground::b)
        .def_readwrite("a", &ecs::UIBackground::a);

    py::class_<ecs::UITexturedBackground>(m, "UITexturedBackground")
        .def_property("path",
            [](const ecs::UITexturedBackground& t) { return std::string(t.path); },
            [](ecs::UITexturedBackground& t, const std::string& s) {
                std::strncpy(t.path, s.c_str(), sizeof(t.path) - 1);
                t.path[sizeof(t.path) - 1] = '\0';
                t.texture = nullptr;  // path changed: force a reload next frame
            })
        .def_property_readonly("has_texture",
            [](const ecs::UITexturedBackground& t) { return t.texture != nullptr; })
        .def_readwrite("tint_r", &ecs::UITexturedBackground::tintR)
        .def_readwrite("tint_g", &ecs::UITexturedBackground::tintG)
        .def_readwrite("tint_b", &ecs::UITexturedBackground::tintB)
        .def_readwrite("tint_a", &ecs::UITexturedBackground::tintA);

    py::class_<ecs::UIText>(m, "UIText")
        .def_property("text",
            [](const ecs::UIText& t) { return std::string(t.text); },
            [](ecs::UIText& t, const std::string& s) {
                std::strncpy(t.text, s.c_str(), sizeof(t.text) - 1);
                t.text[sizeof(t.text) - 1] = '\0';
            })
        .def_property("font",
            [](const ecs::UIText& t) { return std::string(t.fontPath); },
            [](ecs::UIText& t, const std::string& s) {
                std::strncpy(t.fontPath, s.c_str(), sizeof(t.fontPath) - 1);
                t.fontPath[sizeof(t.fontPath) - 1] = '\0';
            })
        .def_readwrite("r", &ecs::UIText::cr)
        .def_readwrite("g", &ecs::UIText::cg)
        .def_readwrite("b", &ecs::UIText::cb)
        .def_readwrite("a", &ecs::UIText::ca)
        .def_readwrite("display_px", &ecs::UIText::displayPx)
        .def_readwrite("alignment",  &ecs::UIText::alignment)
        .def_readwrite("auto_size",  &ecs::UIText::autoSize);

    py::class_<ecs::UIButton>(m, "UIButton")
        .def_readwrite("normal_r", &ecs::UIButton::normalR)
        .def_readwrite("normal_g", &ecs::UIButton::normalG)
        .def_readwrite("normal_b", &ecs::UIButton::normalB)
        .def_readwrite("normal_a", &ecs::UIButton::normalA)
        .def_readwrite("hover_r", &ecs::UIButton::hoverR)
        .def_readwrite("hover_g", &ecs::UIButton::hoverG)
        .def_readwrite("hover_b", &ecs::UIButton::hoverB)
        .def_readwrite("hover_a", &ecs::UIButton::hoverA)
        .def_readwrite("pressed_r", &ecs::UIButton::pressedR)
        .def_readwrite("pressed_g", &ecs::UIButton::pressedG)
        .def_readwrite("pressed_b", &ecs::UIButton::pressedB)
        .def_readwrite("pressed_a", &ecs::UIButton::pressedA)
        .def_readwrite("disabled_r", &ecs::UIButton::disabledR)
        .def_readwrite("disabled_g", &ecs::UIButton::disabledG)
        .def_readwrite("disabled_b", &ecs::UIButton::disabledB)
        .def_readwrite("disabled_a", &ecs::UIButton::disabledA)
        .def_readwrite("enabled", &ecs::UIButton::enabled);

    py::class_<ecs::TextLabel3D>(m, "TextLabel3D")
        .def_property("text",
            [](const ecs::TextLabel3D& t) { return std::string(t.text); },
            [](ecs::TextLabel3D& t, const std::string& s) {
                std::strncpy(t.text, s.c_str(), sizeof(t.text) - 1);
                t.text[sizeof(t.text) - 1] = '\0';
            })
        .def_property("font",
            [](const ecs::TextLabel3D& t) { return std::string(t.fontPath); },
            [](ecs::TextLabel3D& t, const std::string& s) {
                std::strncpy(t.fontPath, s.c_str(), sizeof(t.fontPath) - 1);
                t.fontPath[sizeof(t.fontPath) - 1] = '\0';
            })
        .def_readwrite("r", &ecs::TextLabel3D::cr)
        .def_readwrite("g", &ecs::TextLabel3D::cg)
        .def_readwrite("b", &ecs::TextLabel3D::cb)
        .def_readwrite("a", &ecs::TextLabel3D::ca)
        .def_readwrite("size_meters", &ecs::TextLabel3D::sizeMeters)
        .def_readwrite("billboard",   &ecs::TextLabel3D::billboard);

    {
        py::class_<ecs::Material>(m, "Material")
            .def_property("albedo_map",
                [](const ecs::Material& mat) { return std::string(mat.albedoPath); },
                [](ecs::Material& mat, const std::string& s) {
                    std::strncpy(mat.albedoPath, s.c_str(), sizeof(mat.albedoPath) - 1);
                    mat.albedoPath[sizeof(mat.albedoPath) - 1] = '\0'; mat.resolved = nullptr;
                })
            .def_property("normal_map",
                [](const ecs::Material& mat) { return std::string(mat.normalPath); },
                [](ecs::Material& mat, const std::string& s) {
                    std::strncpy(mat.normalPath, s.c_str(), sizeof(mat.normalPath) - 1);
                    mat.normalPath[sizeof(mat.normalPath) - 1] = '\0'; mat.resolved = nullptr;
                })
            .def_property("metal_rough_map",
                [](const ecs::Material& mat) { return std::string(mat.metalRoughPath); },
                [](ecs::Material& mat, const std::string& s) {
                    std::strncpy(mat.metalRoughPath, s.c_str(), sizeof(mat.metalRoughPath) - 1);
                    mat.metalRoughPath[sizeof(mat.metalRoughPath) - 1] = '\0'; mat.resolved = nullptr;
                })
            .def_property("occlusion_map",
                [](const ecs::Material& mat) { return std::string(mat.occlusionPath); },
                [](ecs::Material& mat, const std::string& s) {
                    std::strncpy(mat.occlusionPath, s.c_str(), sizeof(mat.occlusionPath) - 1);
                    mat.occlusionPath[sizeof(mat.occlusionPath) - 1] = '\0'; mat.resolved = nullptr;
                })
            .def_property("emissive_map",
                [](const ecs::Material& mat) { return std::string(mat.emissivePath); },
                [](ecs::Material& mat, const std::string& s) {
                    std::strncpy(mat.emissivePath, s.c_str(), sizeof(mat.emissivePath) - 1);
                    mat.emissivePath[sizeof(mat.emissivePath) - 1] = '\0'; mat.resolved = nullptr;
                })
            .def_property("albedo",
                [](const ecs::Material& mat) {
                    return py::make_tuple(mat.albedoFactor[0], mat.albedoFactor[1],
                                          mat.albedoFactor[2], mat.albedoFactor[3]);
                },
                [](ecs::Material& mat, py::sequence s) {
                    for (int i = 0; i < 4 && i < (int)py::len(s); ++i) mat.albedoFactor[i] = s[i].cast<float>();
                })
            .def_readwrite("metallic",     &ecs::Material::metallicFactor)
            .def_readwrite("roughness",    &ecs::Material::roughnessFactor)
            .def_readwrite("normal_scale", &ecs::Material::normalScale)
            .def_property("emissive",
                [](const ecs::Material& mat) {
                    return py::make_tuple(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);
                },
                [](ecs::Material& mat, py::sequence s) {
                    for (int i = 0; i < 3 && i < (int)py::len(s); ++i) mat.emissiveFactor[i] = s[i].cast<float>();
                });
    }

    py::class_<ecs::AudioSource>(m, "AudioSource")
        .def_property("path",
            [](const ecs::AudioSource& a) { return std::string(a.path); },
            [](ecs::AudioSource& a, const std::string& s) {
                std::strncpy(a.path, s.c_str(), sizeof(a.path) - 1);
                a.path[sizeof(a.path) - 1] = '\0';
            })
        .def_readwrite("gain",     &ecs::AudioSource::gain)
        .def_readwrite("pitch",    &ecs::AudioSource::pitch)
        .def_readwrite("loop",     &ecs::AudioSource::loop)
        .def_readwrite("autoplay", &ecs::AudioSource::autoplay)
        .def_property_readonly("source",
            [](ecs::AudioSource& a) { return a.source; },
            py::return_value_policy::reference);

    // view(*component_names) → list of tuples (entity, comp1, comp2, ...)
    // The registry is accessed via the module-level 'yope3d.world' attribute.
    // Note: must be called from the main thread while physics is paused.
    m.def("view", [](py::args names) -> py::list {
        auto yope3d = py::module_::import("yope3d");
        auto worldObj = yope3d.attr("world");
        if (worldObj.is_none()) {
            throw std::runtime_error("yope3d.world not bound — call bindContext first");
        }
        auto* world = worldObj.cast<World*>();
        // Lock for the whole build: getRaw walks archetype arrays the physics thread
        // may be migrating (Sleeping-tag adds) during advance().
        auto lock = world->lockStructure();
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
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        void* ptr = world->getRegistry().getRaw(e, PyComponentTable::typeIdForName(name));
        return PyComponentTable::wrapPtr(name, ptr);
    });
    m.def("reg_has", [](ecs::Entity e, const std::string& name) -> bool {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        auto tid = PyComponentTable::typeIdForName(name);
        return world->getRegistry().getRaw(e, tid) != nullptr;
    });
    m.def("reg_valid", [](ecs::Entity e) -> bool {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        return world->getRegistry().valid(e);
    });

    // Tag queries (the question wake() / fix_entity answer). Also reachable via
    // reg_has(e, "Sleeping") / reg_has(e, "Fixed").
    m.def("is_sleeping", [](ecs::Entity e) -> bool {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        return world->getRegistry().has<ecs::Sleeping>(e);
    }, py::arg("entity"));
    m.def("is_fixed", [](ecs::Entity e) -> bool {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        return world->getRegistry().has<ecs::Fixed>(e);
    }, py::arg("entity"));

    // set_text — mutate whichever text component the entity carries (UIText or TextLabel3D).
    m.def("set_text", [](ecs::Entity e, const std::string& s) {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        auto& reg = world->getRegistry();
        if (auto* t = reg.get<ecs::UIText>(e)) {
            std::strncpy(t->text, s.c_str(), sizeof(t->text) - 1);
            t->text[sizeof(t->text) - 1] = '\0';
        } else if (auto* t = reg.get<ecs::TextLabel3D>(e)) {
            std::strncpy(t->text, s.c_str(), sizeof(t->text) - 1);
            t->text[sizeof(t->text) - 1] = '\0';
        }
    }, py::arg("entity"), py::arg("text"));

    // Safe re-resolving accessors — look the component up per call, so they never
    // hold a stale reference across an archetype migration. Prefer these in hot paths.
    m.def("get_position", [](ecs::Entity e) -> py::object {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (auto* tf = world->getRegistry().get<Transform>(e)) return py::cast(tf->position);
        return py::none();
    }, py::arg("entity"));
    m.def("set_position", [](ecs::Entity e, math::Vec3 p) {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (auto* tf = world->getRegistry().get<Transform>(e)) tf->position = p;
    }, py::arg("entity"), py::arg("pos"));
    // World-space position (composes the Parent chain). get_position returns the
    // LOCAL position, which differs from world only for parented entities.
    m.def("get_world_position", [](ecs::Entity e) -> py::object {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (!world->getRegistry().valid(e)) return py::none();
        return py::cast(hierarchy::worldTransform(world->getRegistry(), e).position);
    }, py::arg("entity"));
    m.def("set_velocity", [](ecs::Entity e, math::Vec3 v) {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (auto* h = world->getRegistry().get<ecs::Hull>(e)) h->velocity = v;
    }, py::arg("entity"), py::arg("velocity"));

    // reg_add / reg_remove — change an entity's component composition. Both take the
    // structure lock (composition change = archetype migration vs. the physics thread).
    m.def("reg_add", [](ecs::Entity e, const std::string& name) {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (!PyComponentTable::addByName(world->getRegistry(), e, name))
            throw std::runtime_error("Unknown component: " + name);
    }, py::arg("entity"), py::arg("name"));
    m.def("reg_remove", [](ecs::Entity e, const std::string& name) {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        if (!PyComponentTable::removeByName(world->getRegistry(), e, name))
            throw std::runtime_error("Unknown component: " + name);
    }, py::arg("entity"), py::arg("name"));

    // find_entity — first entity whose Name matches (or None). Linear scan.
    m.def("find_entity", [](const std::string& name) -> py::object {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        for (auto [e, n] : world->getRegistry().view<ecs::Name>()) {
            if (name == n.value) return py::cast(e);
        }
        return py::none();
    }, py::arg("name"));

    // get_behavior — the live Python instance of another entity's behavior, or None.
    // Lets one behavior read/call another's state directly (inter-script comms).
    m.def("get_behavior", [](ecs::Entity e) -> py::object {
        auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
        auto lock = world->lockStructure();
        auto* sc = world->getRegistry().get<ecs::ScriptComponent>(e);
        if (!sc || !sc->instance) return py::none();
        void* h = sc->instance->pyInstanceHandle();
        if (!h) return py::none();
        return py::reinterpret_borrow<py::object>(reinterpret_cast<PyObject*>(h));
    }, py::arg("entity"));

    // attach_script — create + immediately instantiate a Python behavior on an
    // entity at runtime (not just at scene load). Unlike scene-authored
    // ScriptComponents, which SceneManager instantiates once during load/Play,
    // this lets a script spawn N entities and give each one live behavior right
    // away -- init() runs before this call returns, and update()/
    // on_collision_enter/exit dispatch normally from the next frame on.
    //
    // module/class_name select the Python behavior (scripts/behaviors/<module>.py,
    // same as a scene file's paramsBlob "module"/"class" keys). params is merged
    // into the dict the new instance's init() receives.
    //
    // Returns False (no-op) if the entity is invalid or already carries a live
    // script instance -- attach_script does not replace/reset an existing one.
    m.def("attach_script",
        [](ecs::Entity e, const std::string& module_, const std::string& className,
           py::dict params) -> bool {
            auto* world = py::module_::import("yope3d").attr("world").cast<World*>();
            auto* sceneManager = py::module_::import("yope3d").attr("scene_manager").cast<SceneManager*>();
            auto* ctx = PythonInterpreter::boundContext();
            if (!ctx) return false;

            py::dict blob = params.attr("copy")().cast<py::dict>();  // don't mutate caller's dict
            blob["module"] = module_;
            blob["class"]  = className;
            std::string blobStr = py::module_::import("json").attr("dumps")(blob).cast<std::string>();
            if (blobStr.size() >= sizeof(ecs::ScriptComponent::paramsBlob))
                throw std::runtime_error("attach_script: params too large for paramsBlob (2048 bytes)");

            {
                auto lock = world->lockStructure();
                auto& reg = world->getRegistry();
                if (!reg.valid(e)) return false;
                auto* sc = reg.get<ecs::ScriptComponent>(e);
                if (sc && sc->instance) return false;   // already scripted -- no-op
                if (!sc) sc = &reg.add<ecs::ScriptComponent>(e);
                std::strncpy(sc->scriptClass, "PythonScript", sizeof(sc->scriptClass) - 1);
                sc->scriptClass[sizeof(sc->scriptClass) - 1] = '\0';
                std::strncpy(sc->paramsBlob, blobStr.c_str(), sizeof(sc->paramsBlob) - 1);
                sc->paramsBlob[sizeof(sc->paramsBlob) - 1] = '\0';
            }
            return sceneManager->instantiateScript(e, *ctx);
        }, py::arg("entity"), py::arg("module"), py::arg("class_name"),
           py::arg("params") = py::dict());
}
#endif // YOPE_PYTHON
