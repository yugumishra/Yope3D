#pragma once
#include "AnimationClip.h"
#include "../world/Transform.h"
#include "../math/Mat4.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Skeleton — bind-pose bone hierarchy plus the pose pipeline that turns an
// anim::Clip into a GPU joint palette.
//
// Bones are deliberately NOT ECS entities. A 60-bone humanoid would otherwise be
// 60 Transforms rewritten every tick, 60 rows in the editor hierarchy, and 60
// entries through publishSnapshot and Play/Stop restore — for data the sim
// rewrites wholesale every frame. That is the same reasoning that keeps
// anim::Clip out of the registry (see AnimationClip.h) and that turned the
// Sleeping tag into the Hull.asleep flag. Skeletons live in a World-side store;
// per-bone entities exist only where something must attach to one, via
// ecs::BoneAttachment.
//
// This header and Skeleton.cpp are kept free of engine includes (no ECS, no
// World, no Vulkan) so yope_animation_tests can link them against nothing but
// math — the same discipline TextLayout.cpp follows for yope_text_tests.
//
// CHANNEL INDEX SPACE: when an anim::Clip drives a Skeleton, Channel::targetNode
// is a BONE index into this Skeleton, not a LoadedModel node index as it is on
// the rigid path. glTF authors channels against node indices, so the importer
// inverts LoadedSkin::joints (bone -> node) to remap them on the way in. Getting
// this backwards binds every channel to the wrong bone.
// ---------------------------------------------------------------------------

namespace anim {

struct Skeleton {
    // parent[i] is the index of bone i's parent, or -1 for a root. Bones are
    // stored in topological order so parent[i] < i always holds — that ordering
    // is what lets buildPalette resolve the whole hierarchy in one forward pass
    // with no recursion and no visited set.
    std::vector<int>         parent;
    std::vector<Transform>   bindLocal;    // rest pose, parent-relative
    std::vector<math::Mat4>  inverseBind;  // glTF inverseBindMatrices
    std::vector<std::string> names;        // socket lookup + editor display

    size_t boneCount() const { return parent.size(); }

    // True when every parent precedes its child and all indices are in range.
    // buildPalette's correctness depends on this; check it once at import rather
    // than per frame.
    bool isTopologicallyOrdered() const;
};

// Index of the bone with this name, or -1. Linear — intended for setup (socket
// resolution, editor lookups), not per-frame use.
int findBone(const Skeleton& sk, const std::string& name);

// Sample `clip` at `time` into a parent-relative local pose.
//
// Every bone starts at its bind-pose local transform and is then overwritten by
// whichever channels target it, per glTF semantics: a sampled value IS the local
// TRS for that path, not a delta. Bones the clip never mentions therefore hold
// their bind pose, and a clip that animates only rotation leaves bind translation
// and scale intact.
//
// `out` is resized to boneCount().
void samplePose(const Skeleton& sk, const Clip& clip, float time,
                std::vector<Transform>& out);

// Per-bone blend of two local poses: `w` = 0 yields `a` exactly, 1 yields `b`.
// Translation and scale lerp; rotation slerps (a component-wise lerp would not
// stay on the unit arc). This is the whole of v1 blending — a two-clip cross-fade,
// no blend trees or additive layers.
//
// `out` may alias `a` or `b`. Sizes are taken from `a`.
void blendPoses(const std::vector<Transform>& a, const std::vector<Transform>& b,
                float w, std::vector<Transform>& out);

// Compose a local pose into per-bone WORLD (skeleton-space) matrices:
//
//     world[i] = parent[i] < 0 ? local[i] : world[parent[i]] * local[i]
//
// One forward pass, relying on the topological-order invariant. This is what a
// socket wants — where the bone actually IS — as opposed to buildPalette's
// output, which is the skinning matrix world[i] * inverseBind[i] and is only
// meaningful applied to bind-pose vertices. Attaching a prop with a palette
// matrix instead of a world matrix puts it at the bone's bind-pose offset from
// itself, which looks almost right and is never right.
void buildWorldPose(const Skeleton& sk, const std::vector<Transform>& localPose,
                    std::vector<math::Mat4>& out);

// Compose a local pose into the GPU joint palette:
//
//     world[i]   = parent[i] < 0 ? local[i] : world[parent[i]] * local[i]
//     palette[i] = world[i] * inverseBind[i]
//
// Runs as two in-place passes over `out` (world matrices, then right-multiply by
// the inverse binds), so it allocates nothing beyond the one resize and needs no
// scratch buffer.
//
// The palette is deliberately MODEL-FREE: it carries no entity model matrix, so
// compute skinning emits object-space vertices and every downstream pass applies
// push.model exactly as it does for static geometry. Baking the model matrix in
// here is the classic double-transform bug — do not.
void buildPalette(const Skeleton& sk, const std::vector<Transform>& localPose,
                  std::vector<math::Mat4>& out);

} // namespace anim
