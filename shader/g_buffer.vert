#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 5) in uint aPacked;        // x4|y4|z4|face3|orient4|reserved13
layout(location = 7) in uint aBlockType;     // 16-bit
layout(location = 8) in uint aTextureLayer;  // 16-bit

// 与 m_drawCommands 同序：第 i 条 indirect 命令对应 sectionBases[i]
layout(std430, binding = 0) readonly buffer SectionBases {
    vec4 sectionBases[];   // xyz = (chunkX*16, sectionY*16, chunkZ*16); w 保留
};

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
flat out int vBlockType;
flat out int vFaceIndex;
flat out int vTextureLayer;

uniform mat4 uView;
uniform mat4 uProjection;

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
    // 解包局部坐标 + 面索引
    int lx   = int( aPacked        & 0xFu);
    int ly   = int((aPacked >> 4)  & 0xFu);
    int lz   = int((aPacked >> 8)  & 0xFu);
    int face = int((aPacked >> 12) & 0x7u);

    // 还原方块中心世界坐标：sectionBase + 局部 + 0.5
    vec3 sectionBase = sectionBases[gl_DrawID].xyz;
    vec3 blockPos = sectionBase + vec3(float(lx), float(ly), float(lz)) + vec3(0.5);

    mat4 faceMatrix = faceMatrices[face];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(blockPos, 1.0);
    mat4 model = translation * faceMatrix;

    vWorldPos = (model * vec4(aPos, 1.0)).xyz;
    vNormal = mat3(faceMatrix) * aNormal;
    vTexCoord = aTexCoord;

    vBlockType = int(aBlockType);
    vFaceIndex = face;
    vTextureLayer = int(aTextureLayer);

    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
