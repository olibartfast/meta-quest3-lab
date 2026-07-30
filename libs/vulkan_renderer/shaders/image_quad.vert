#version 450

layout(push_constant) uniform PushConstants {
    mat4 modelViewProjection;
} pushConstants;

layout(location = 0) out vec2 textureCoordinate;

const vec2 positions[6] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

const vec2 textureCoordinates[6] = vec2[](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 0.0)
);

void main() {
    gl_Position = pushConstants.modelViewProjection *
        vec4(positions[gl_VertexIndex], 0.0, 1.0);
    textureCoordinate = textureCoordinates[gl_VertexIndex];
}
