#version 450

// A/B split: draws the B image (rendered by a second pipeline with the compare settings) right of the divider, over
// the A image already in the target, with a thin divider line. RenderPipeline::Build in src/rendering/render_pipeline.cpp.
layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D u_compareImage;

layout(push_constant) uniform PushConstants
{
    float u_split;      // fraction of the width where B starts
    float u_halfLine;   // divider half-width as a fraction of the width
};

const vec3 DIVIDER_COLOR = vec3(1.0, 1.0, 1.0);

void main() {
    float offset = TexCoord.x - u_split;
    if (offset < -u_halfLine) discard;
    if (offset < u_halfLine) {
        FragColor = vec4(DIVIDER_COLOR, 1.0);
        return;
    }
    FragColor = vec4(texture(u_compareImage, TexCoord).rgb, 1.0);
}
