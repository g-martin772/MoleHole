#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 hitValue;

hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = vec4(barycentricCoords, gl_HitTEXT);
}
