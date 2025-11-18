#version 460 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D u_raytracedImage;
uniform sampler2D u_bloomImage;
uniform int u_bloomEnabled = 1;
uniform float u_bloomIntensity = 5.0;
uniform int u_bloomDebug = 0; // Debug mode: 0=normal, 1=show bloom only, 2=show extraction only

void main() {
    vec3 color = texture(u_raytracedImage, TexCoord).rgb;
    vec3 bloomColor = texture(u_bloomImage, TexCoord).rgb;
    
    if (u_bloomDebug == 1) {
        FragColor = vec4(bloomColor * u_bloomIntensity, 1.0);
    } else if (u_bloomDebug == 2) {
        FragColor = vec4(bloomColor * 10.0, 1.0);
    } else if (u_bloomEnabled == 1) {
        color += bloomColor * u_bloomIntensity;
        FragColor = vec4(color, 1.0);
    } else {
        FragColor = vec4(color, 1.0);
    }
}
