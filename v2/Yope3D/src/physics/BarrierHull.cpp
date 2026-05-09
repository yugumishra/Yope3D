#include "BarrierHull.h"

namespace physics {

BarrierHull::BarrierHull(std::vector<std::variant<Barrier, BoundedBarrier>> bs,
                         math::Vec3 pos, math::Vec3 ext)
    : Hull(pos, {}, 1.0f), barriers(std::move(bs)), extent(ext)
{
    fix();
    initiateState();
}

BarrierHull BarrierHull::genRectangularBarriers(math::Vec3 ext, math::Vec3 pos) {
    std::vector<std::variant<Barrier, BoundedBarrier>> bs;
    bs.emplace_back(Barrier{{0,  1, 0}, pos - math::Vec3{0, ext.y, 0}});   // floor
    bs.emplace_back(Barrier{{0, -1, 0}, pos + math::Vec3{0, ext.y, 0}});   // ceiling
    bs.emplace_back(Barrier{{ 1, 0, 0}, pos - math::Vec3{ext.x, 0, 0}});   // left
    bs.emplace_back(Barrier{{-1, 0, 0}, pos + math::Vec3{ext.x, 0, 0}});   // right
    bs.emplace_back(Barrier{{0,  0,  1}, pos - math::Vec3{0, 0, ext.z}});  // back
    bs.emplace_back(Barrier{{0,  0, -1}, pos + math::Vec3{0, 0, ext.z}});  // front
    return BarrierHull(std::move(bs), pos, ext);
}

} // namespace physics
