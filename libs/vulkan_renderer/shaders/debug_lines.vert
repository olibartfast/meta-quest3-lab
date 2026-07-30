#version 450

layout(push_constant) uniform PushConstants {
    mat4 modelViewProjection;
    vec4 color;
    int shape;
} pushConstants;

layout(location = 0) out vec3 fragmentColor;

const vec3 axisPositions[6] = vec3[](
    vec3(0.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0)
);

const vec3 axisColors[6] = vec3[](
    vec3(1.0, 0.1, 0.1), vec3(1.0, 0.1, 0.1),
    vec3(0.1, 1.0, 0.1), vec3(0.1, 1.0, 0.1),
    vec3(0.1, 0.35, 1.0), vec3(0.1, 0.35, 1.0)
);

const vec3 rectanglePositions[8] = vec3[](
    vec3(-0.5, 0.0, -0.5), vec3( 0.5, 0.0, -0.5),
    vec3( 0.5, 0.0, -0.5), vec3( 0.5, 0.0,  0.5),
    vec3( 0.5, 0.0,  0.5), vec3(-0.5, 0.0,  0.5),
    vec3(-0.5, 0.0,  0.5), vec3(-0.5, 0.0, -0.5)
);

const vec3 screenRectanglePositions[8] = vec3[](
    vec3(-0.5, -0.5, 0.0), vec3( 0.5, -0.5, 0.0),
    vec3( 0.5, -0.5, 0.0), vec3( 0.5,  0.5, 0.0),
    vec3( 0.5,  0.5, 0.0), vec3(-0.5,  0.5, 0.0),
    vec3(-0.5,  0.5, 0.0), vec3(-0.5, -0.5, 0.0)
);

const vec3 rayPositions[2] = vec3[](
    vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0)
);

const vec3 boxPositions[24] = vec3[](
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5),
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3(-0.5, -0.5, -0.5),
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5),
    vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5)
);

void main() {
    vec3 position;
    if (pushConstants.shape == 0) {
        position = axisPositions[gl_VertexIndex];
        fragmentColor = axisColors[gl_VertexIndex];
    } else if (pushConstants.shape == 1) {
        position = rectanglePositions[gl_VertexIndex];
        fragmentColor = pushConstants.color.rgb;
    } else if (pushConstants.shape == 2) {
        position = rayPositions[gl_VertexIndex];
        fragmentColor = pushConstants.color.rgb;
    } else if (pushConstants.shape == 3) {
        position = boxPositions[gl_VertexIndex];
        fragmentColor = pushConstants.color.rgb;
    } else {
        position = screenRectanglePositions[gl_VertexIndex];
        fragmentColor = pushConstants.color.rgb;
    }
    gl_Position =
        pushConstants.modelViewProjection * vec4(position, 1.0);
}
