#version 460 core

uniform mat4 lightSpaceMatrix;
layout(location = 0) in vec3 aPos;
layout(location = 5) in uint aPacked;        // CUBE: x4|y4|z4|face3|orient4|… / MODEL: x4|y4|z4|quadId19|flag
layout(location = 7) in uint aBlockType;     // 16-bit

layout(std430, binding = 0) readonly buffer SectionBases {
    vec4 sectionBases[];
};

// 模型 quad 表（binding=4）。阴影 pass 只需要角点位置。
struct Quad {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 n;
    vec4 uv01;
    vec4 uv23;
    uvec4 meta;
};
layout(std430, binding = 4) readonly buffer QuadTable {
    Quad quads[];
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

    int lx = int( aPacked        & 0xFu);
    int ly = int((aPacked >> 4)  & 0xFu);
    int lz = int((aPacked >> 8)  & 0xFu);

    vec3 sectionBase = sectionBases[gl_DrawID].xyz;
    vec3 blockOrigin = sectionBase + vec3(float(lx), float(ly), float(lz));

    if ((aPacked & 0x80000000u) != 0u) {
        // 模型 quad：角点直接从表里取
        uint qid = (aPacked >> 12) & 0x7FFFu;
        Quad q = quads[qid];
        vec4 corner = (gl_VertexID == 0) ? q.p0 :
                      (gl_VertexID == 1) ? q.p1 :
                      (gl_VertexID == 2) ? q.p2 : q.p3;
        vec3 worldPos = blockOrigin + corner.xyz;
        gl_Position = lightSpaceMatrix * vec4(worldPos, 1.0);
        return;
    }

    int face = int((aPacked >> 12) & 0x7u);
    vec3 blockPos = blockOrigin + vec3(0.5);

    mat4 faceMatrix = faceMatrices[face];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(blockPos, 1.0);
    mat4 model = translation * faceMatrix;
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
