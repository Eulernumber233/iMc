#version 460 core

uniform mat4 lightSpaceMatrix;
layout(location = 0) in vec3 aPos;
layout(location = 5) in uint aPacked;        // x4|y4|z4|face3|orient4|reserved13
layout(location = 7) in uint aBlockType;     // 16-bit

layout(std430, binding = 0) readonly buffer SectionBases {
    vec4 sectionBases[];
};

flat out int vBlockType;

const mat4 faceMatrices[6] = mat4[](
    // 0: RIGHT (+X)
    mat4(0,0,-1,0, 0,1,0,0, 1,0,0,0, 0.5,0,0,1),
    // 1: LEFT (-X)
    mat4(0,0,1,0, 0,1,0,0, -1,0,0,0, -0.5,0,0,1),
    // 2: FRONT (+Z)
    mat4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0.5,1),
    // 3: BACK (-Z)
    mat4(-1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,-0.5,1),
    // 4: UP (+Y)
    mat4(1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0.5,0,1),
    // 5: DOWN (-Y)
    mat4(1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,-0.5,0,1)
);

void main() {
    vBlockType = int(aBlockType);

    int lx   = int( aPacked        & 0xFu);
    int ly   = int((aPacked >> 4)  & 0xFu);
    int lz   = int((aPacked >> 8)  & 0xFu);
    int face = int((aPacked >> 12) & 0x7u);

    vec3 sectionBase = sectionBases[gl_DrawID].xyz;
    vec3 blockPos = sectionBase + vec3(float(lx), float(ly), float(lz)) + vec3(0.5);

    mat4 faceMatrix = faceMatrices[face];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(blockPos, 1.0);
    mat4 model = translation * faceMatrix;
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
