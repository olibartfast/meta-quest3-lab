#version 450

layout(push_constant) uniform PushConstants {
    mat4 modelViewProjection;
} pushConstants;

layout(location = 0) out vec3 fragmentColor;

const vec3 positions[3] = vec3[](
    vec3(-0.55, -0.45, 0.0),
    vec3( 0.55, -0.45, 0.0),
    vec3( 0.00,  0.55, 0.0)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.15, 0.10),
    vec3(0.10, 1.0, 0.20),
    vec3(0.10, 0.35, 1.0)
);

void main() {
    gl_Position =
        pushConstants.modelViewProjection * vec4(positions[gl_VertexIndex], 1.0);
    fragmentColor = colors[gl_VertexIndex];
}
