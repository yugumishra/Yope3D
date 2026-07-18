#pragma once
#include "../math/Vec3.h"
#include "../math/Quat.h"
#include "../math/Mat3.h"
#include "../math/Mat4.h"

struct Transform {
    math::Vec3 position {0.0f, 0.0f, 0.0f};
    math::Quat rotation {0.0f, 0.0f, 0.0f, 1.0f};
    math::Vec3 scale    {1.0f, 1.0f, 1.0f};

    math::Mat4 getModelMatrix() const {
        math::Mat4 T = math::Mat4::translate(position);
        math::Mat4 R;
        R.setRotationScale(math::Mat3::rotation(rotation));
        math::Mat4 S = math::Mat4::scale(scale);
        return T * R * S;
    }
};
