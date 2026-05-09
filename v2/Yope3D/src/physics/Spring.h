#pragma once
#include "Hull.h"

namespace physics {

class Spring {
public:
    Spring(Hull* first, Hull* second, float k, float restLength);
    void update(float dt);

private:
    Hull*  first;
    Hull*  second;
    float  k;
    float  restLength;
};

} // namespace physics
