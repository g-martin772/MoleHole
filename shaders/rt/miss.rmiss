#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 hitValue;

void main()
{
    hitValue = vec4(0.0, 0.0, 0.2, -1.0); // w < 0 indicates miss
}
