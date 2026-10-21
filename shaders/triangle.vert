#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aPosLayer; // xyz = world position, w = texture array layer
layout (location = 3) in vec4 aRot;      // quaternion (x, y, z, w)
layout (location = 4) in vec4 aScale;    // xyz = scale

out vec2 TexCoord;
flat out float TexLayer;

uniform mat4 u_view;
uniform mat4 u_proj;

// Rotate v by unit quaternion q
vec3 quat_rotate(vec4 q, vec3 v)
{
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main()
{
    vec3 world = aPosLayer.xyz + quat_rotate(aRot, aPos * aScale.xyz);
    gl_Position = u_proj * u_view * vec4(world, 1.0);
    TexCoord = aTexCoord;
    TexLayer = aPosLayer.w;
}
