#version 430 core

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 2) buffer data {
    vec4 spheres[];
};

layout (location = 1) uniform float dt;
layout (location = 3) uniform int iterations;

void main() {
    for(int i= 0; i< iterations; i++) {
    vec3 position = spheres[gl_WorkGroupID.x*2].xyz;
    float r = spheres[gl_WorkGroupID.x*2].w;
    vec3 velocity = spheres[gl_WorkGroupID.x * 2 + 1].xyz;

    //apply all forces acting on the sphere
    if(position.y < r) {
        //we are under the floor
        position.y = r;
        velocity.y *= -0.9;
    }else {
        //we are above the floor
        velocity.y -= 9.81*dt;
    }

    //integrate
    position += velocity*dt;
    

    //reassign
    spheres[gl_WorkGroupID.x * 2+0].x = position.x;
    spheres[gl_WorkGroupID.x * 2+0].y = position.y;
    spheres[gl_WorkGroupID.x * 2+0].z = position.z;
    
    spheres[gl_WorkGroupID.x * 2+1].x = velocity.x;
    spheres[gl_WorkGroupID.x * 2+1].y = velocity.y;
    spheres[gl_WorkGroupID.x * 2+1].z = velocity.z;
    }
}