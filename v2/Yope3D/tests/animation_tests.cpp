#include <catch2/catch_test_macros.hpp>
#include "../src/assets/Skeleton.h"
#include <cmath>
#include <vector>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// Skeletal pose pipeline: sampling, cross-fade blending, joint palette.
// Headless by construction — Skeleton.cpp pulls in nothing but math, so every
// claim here is provable without a GPU, an ECS registry, or a World.
// ---------------------------------------------------------------------------

static bool approx(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static math::Vec3 xform(const math::Mat4& m, math::Vec3 p) {
    math::Vec4 r = m * math::Vec4{ p.x, p.y, p.z, 1.0f };
    return { r.x, r.y, r.z };
}

static bool isIdentity(const math::Mat4& m) {
    const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int i = 0; i < 16; ++i) if (!approx(m.m[i], I[i])) return false;
    return true;
}

static math::Quat rotZ(float degrees) {
    return math::Quat::fromAxisAngle(math::Vec3{0, 0, 1}, degrees * 3.14159265358979f / 180.0f);
}

// A 3-bone vertical chain: root at the origin, then two 1-unit segments up +Y.
// Bind world positions are therefore y = 0, 1, 2, and the inverse binds are the
// exact inverses of those — which is what makes "bind pose => identity palette"
// a real assertion rather than a tautology.
static anim::Skeleton makeChain() {
    anim::Skeleton sk;
    sk.names  = { "root", "upper", "lower" };
    sk.parent = { -1, 0, 1 };

    Transform root, upper, lower;
    upper.position = { 0, 1, 0 };
    lower.position = { 0, 1, 0 };
    sk.bindLocal = { root, upper, lower };

    // Bind world = translate(0, 0/1/2, 0); inverse binds undo exactly that.
    sk.inverseBind = {
        math::Mat4::translate({ 0,  0, 0 }),
        math::Mat4::translate({ 0, -1, 0 }),
        math::Mat4::translate({ 0, -2, 0 }),
    };
    return sk;
}

// A clip holding one bone at a constant rotation. A single keyframe is enough:
// the samplers clamp outside the key range, so this evaluates to `q` at any time.
static anim::Clip rotationClip(int bone, math::Quat q, const char* name = "pose") {
    anim::Clip clip;
    clip.name     = name;
    clip.duration = 0.0f;

    anim::Channel ch;
    ch.targetNode = bone;
    ch.path       = anim::Path::Rotation;
    ch.interp     = anim::Interp::Linear;
    ch.times      = { 0.0f };
    ch.values     = { q.x, q.y, q.z, q.w };
    clip.channels.push_back(std::move(ch));
    return clip;
}

TEST_CASE("skeleton: topological order and bone lookup", "[anim][skeleton]") {
    anim::Skeleton sk = makeChain();
    CHECK(sk.boneCount() == 3);
    CHECK(sk.isTopologicallyOrdered());

    CHECK(anim::findBone(sk, "root")  == 0);
    CHECK(anim::findBone(sk, "lower") == 2);
    CHECK(anim::findBone(sk, "absent") == -1);

    // A forward reference breaks buildPalette's single-pass assumption, so it must
    // be detectable at import rather than producing a silently wrong palette.
    anim::Skeleton bad = sk;
    bad.parent = { 1, -1, 1 };
    CHECK_FALSE(bad.isTopologicallyOrdered());

    // Mismatched array lengths are equally fatal and equally silent.
    anim::Skeleton ragged = sk;
    ragged.inverseBind.pop_back();
    CHECK_FALSE(ragged.isTopologicallyOrdered());
}

TEST_CASE("palette: bind pose yields identity for every bone", "[anim][palette]") {
    anim::Skeleton sk = makeChain();
    std::vector<math::Mat4> palette;
    anim::buildPalette(sk, sk.bindLocal, palette);

    REQUIRE(palette.size() == 3);
    for (size_t i = 0; i < palette.size(); ++i) {
        INFO("bone " << i);
        CHECK(isIdentity(palette[i]));
    }
}

TEST_CASE("palette: rotating one bone moves the vertices bound to it", "[anim][palette]") {
    anim::Skeleton sk = makeChain();

    // Rotate the middle bone 90 degrees about +Z. The chain tip, bound at (0,2,0),
    // should swing to (-1,1,0): the elbow at y=1 stays put and the 1-unit segment
    // above it rotates +Y onto -X.
    std::vector<Transform> pose = sk.bindLocal;
    pose[1].rotation = rotZ(90.0f);

    std::vector<math::Mat4> palette;
    anim::buildPalette(sk, pose, palette);
    REQUIRE(palette.size() == 3);

    // The root is unaffected by a child's rotation.
    CHECK(isIdentity(palette[0]));

    math::Vec3 tip = xform(palette[1], { 0, 2, 0 });
    CHECK(approx(tip.x, -1.0f));
    CHECK(approx(tip.y,  1.0f));
    CHECK(approx(tip.z,  0.0f));

    // A vertex at the joint itself is the rotation's fixed point.
    math::Vec3 elbow = xform(palette[1], { 0, 1, 0 });
    CHECK(approx(elbow.x, 0.0f));
    CHECK(approx(elbow.y, 1.0f));
}

TEST_CASE("palette: parent rotation propagates down the chain", "[anim][palette]") {
    anim::Skeleton sk = makeChain();

    // Rotate the ROOT 90 degrees about +Z. Bone 2's palette then reduces to that
    // same rotation, so its bind-pose vertex at (0,2,0) lands on (-2,0,0). This is
    // what fails if buildPalette composes the hierarchy wrongly — or if the two
    // passes are fused and children read their parent's palette instead of its
    // world matrix.
    std::vector<Transform> pose = sk.bindLocal;
    pose[0].rotation = rotZ(90.0f);

    std::vector<math::Mat4> palette;
    anim::buildPalette(sk, pose, palette);
    REQUIRE(palette.size() == 3);

    math::Vec3 tip = xform(palette[2], { 0, 2, 0 });
    CHECK(approx(tip.x, -2.0f));
    CHECK(approx(tip.y,  0.0f));
    CHECK(approx(tip.z,  0.0f));

    math::Vec3 mid = xform(palette[1], { 0, 1, 0 });
    CHECK(approx(mid.x, -1.0f));
    CHECK(approx(mid.y,  0.0f));
}

TEST_CASE("worldPose: sockets get where the bone IS, not its skinning matrix", "[anim][palette]") {
    // buildWorldPose stops one step short of buildPalette. The distinction is the
    // whole reason BoneAttachment works: a socket wants the bone's world matrix,
    // while the palette matrix (world * inverseBind) is only meaningful applied to
    // bind-pose vertices. Using the palette for a socket puts the prop at the
    // bone's own bind offset from itself — plausible-looking and always wrong.
    anim::Skeleton sk = makeChain();

    std::vector<math::Mat4> world, palette;
    anim::buildWorldPose(sk, sk.bindLocal, world);
    anim::buildPalette  (sk, sk.bindLocal, palette);
    REQUIRE(world.size() == 3);

    // At bind pose the palette is identity everywhere, but the world pose is NOT:
    // it still carries each bone's rest placement up the chain.
    CHECK(isIdentity(palette[2]));
    CHECK(approx(world[1].m[13], 1.0f));   // bone 1 rests at y = 1
    CHECK(approx(world[2].m[13], 2.0f));   // bone 2 at y = 2

    // Bend the middle bone: bone 2 rides it, so its origin swings to (-1,1,0).
    std::vector<Transform> pose = sk.bindLocal;
    pose[1].rotation = rotZ(90.0f);
    anim::buildWorldPose(sk, pose, world);

    math::Vec3 socket = xform(world[2], { 0, 0, 0 });
    CHECK(approx(socket.x, -1.0f));
    CHECK(approx(socket.y,  1.0f));
    CHECK(approx(socket.z,  0.0f));

    // The root is unmoved by a child's rotation.
    math::Vec3 root = xform(world[0], { 0, 0, 0 });
    CHECK(approx(root.x, 0.0f));
    CHECK(approx(root.y, 0.0f));
}

TEST_CASE("samplePose: bones the clip ignores keep their bind pose", "[anim][sample]") {
    anim::Skeleton sk = makeChain();
    anim::Clip clip = rotationClip(1, rotZ(90.0f));

    std::vector<Transform> pose;
    anim::samplePose(sk, clip, 0.0f, pose);
    REQUIRE(pose.size() == 3);

    // Bones 0 and 2 are untouched by the clip.
    CHECK(approx(pose[0].rotation.w, 1.0f));
    CHECK(approx(pose[2].rotation.w, 1.0f));
    CHECK(approx(pose[2].position.y, 1.0f));

    // Bone 1 is rotated — but a rotation-only channel must NOT clobber the bind
    // translation, or the whole skeleton collapses to the origin.
    CHECK(approx(pose[1].position.y, 1.0f));
    CHECK(approx(pose[1].rotation.z, std::sin(45.0f * 3.14159265358979f / 180.0f)));

    // Out-of-range channels are skipped rather than corrupting memory.
    anim::Clip stray = rotationClip(99, rotZ(90.0f));
    std::vector<Transform> unchanged;
    anim::samplePose(sk, stray, 0.0f, unchanged);
    REQUIRE(unchanged.size() == 3);
    CHECK(approx(unchanged[1].rotation.w, 1.0f));
}

TEST_CASE("samplePose: resampling is pure, not cumulative", "[anim][sample]") {
    // Sampling into a reused buffer must fully overwrite it. If samplePose ever
    // composed onto the previous contents instead of starting from bind, a held
    // pose would drift a little further every frame.
    anim::Skeleton sk = makeChain();
    anim::Clip clip = rotationClip(1, rotZ(90.0f));

    std::vector<Transform> pose;
    anim::samplePose(sk, clip, 0.0f, pose);
    const math::Quat first = pose[1].rotation;

    for (int i = 0; i < 8; ++i) anim::samplePose(sk, clip, 0.0f, pose);

    CHECK(approx(pose[1].rotation.x, first.x));
    CHECK(approx(pose[1].rotation.y, first.y));
    CHECK(approx(pose[1].rotation.z, first.z));
    CHECK(approx(pose[1].rotation.w, first.w));
    CHECK(approx(pose[1].position.y, 1.0f));
}

TEST_CASE("blendPoses: endpoints reproduce each source pose exactly", "[anim][blend]") {
    anim::Skeleton sk = makeChain();

    std::vector<Transform> a, b;
    anim::samplePose(sk, rotationClip(1, rotZ(0.0f)),  0.0f, a);
    anim::samplePose(sk, rotationClip(1, rotZ(90.0f)), 0.0f, b);

    std::vector<Transform> out;

    // Exact equality, not approx: the implementation short-circuits at w == 0 and
    // w == 1 precisely so a held or just-started cross-fade cannot perturb the
    // pose in the low bits. Comparing approximately here would let that
    // short-circuit be deleted without the test noticing.
    anim::blendPoses(a, b, 0.0f, out);
    REQUIRE(out.size() == a.size());
    for (size_t i = 0; i < out.size(); ++i) {
        INFO("bone " << i);
        CHECK(out[i].rotation.x == a[i].rotation.x);
        CHECK(out[i].rotation.w == a[i].rotation.w);
        CHECK(out[i].position.y == a[i].position.y);
        CHECK(out[i].scale.x    == a[i].scale.x);
    }

    anim::blendPoses(a, b, 1.0f, out);
    REQUIRE(out.size() == b.size());
    for (size_t i = 0; i < out.size(); ++i) {
        INFO("bone " << i);
        CHECK(out[i].rotation.z == b[i].rotation.z);
        CHECK(out[i].rotation.w == b[i].rotation.w);
        CHECK(out[i].position.y == b[i].position.y);
    }
}

TEST_CASE("blendPoses: midpoint slerps rotation and lerps translation", "[anim][blend]") {
    anim::Skeleton sk = makeChain();

    std::vector<Transform> a = sk.bindLocal;
    std::vector<Transform> b = sk.bindLocal;
    a[1].rotation = rotZ(0.0f);
    b[1].rotation = rotZ(90.0f);
    a[1].position = { 0, 1, 0 };
    b[1].position = { 0, 3, 0 };

    std::vector<Transform> out;
    anim::blendPoses(a, b, 0.5f, out);

    // Halfway along the arc is 45 degrees, not the component-wise mean of the two
    // quaternions (which would not be unit-length).
    const float s45 = std::sin(22.5f * 3.14159265358979f / 180.0f);
    CHECK(approx(out[1].rotation.z, s45));
    CHECK(approx(out[1].rotation.w, std::cos(22.5f * 3.14159265358979f / 180.0f)));

    // Translation is a plain lerp.
    CHECK(approx(out[1].position.y, 2.0f));
    CHECK(approx(out[1].scale.x,    1.0f));
}

TEST_CASE("blendPoses: a cross-fade drives the palette through the arc", "[anim][blend][palette]") {
    // End to end: two clips, blended, composed to a palette, checked by where a
    // vertex actually lands. At w=1 the tip must sit exactly where the 90-degree
    // pose puts it in the single-pose test above.
    anim::Skeleton sk = makeChain();

    std::vector<Transform> rest, bent, mixed;
    anim::samplePose(sk, rotationClip(1, rotZ(0.0f)),  0.0f, rest);
    anim::samplePose(sk, rotationClip(1, rotZ(90.0f)), 0.0f, bent);

    std::vector<math::Mat4> palette;

    anim::blendPoses(rest, bent, 0.0f, mixed);
    anim::buildPalette(sk, mixed, palette);
    CHECK(isIdentity(palette[1]));

    anim::blendPoses(rest, bent, 1.0f, mixed);
    anim::buildPalette(sk, mixed, palette);
    math::Vec3 tip = xform(palette[1], { 0, 2, 0 });
    CHECK(approx(tip.x, -1.0f));
    CHECK(approx(tip.y,  1.0f));

    // Halfway: 45 degrees puts the tip on the unit circle around the elbow.
    anim::blendPoses(rest, bent, 0.5f, mixed);
    anim::buildPalette(sk, mixed, palette);
    math::Vec3 half = xform(palette[1], { 0, 2, 0 });
    const float r = std::sqrt(half.x * half.x + (half.y - 1.0f) * (half.y - 1.0f));
    CHECK(approx(r, 1.0f));
    CHECK(approx(half.x, -std::sin(45.0f * 3.14159265358979f / 180.0f)));
}
