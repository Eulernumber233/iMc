#version 460 core

// ── 烘焙路径顶点着色器 ────────────────────────────────────────────────
// 输入是 CPU 已经展开好的顶点流：每个可见面 4 个顶点，几何已是 section
// 局部坐标。这里只做「section 原点 + 局部坐标」的平移，再乘视图/投影。
//
// 对比实例化路径（g_buffer.vert）：那边每个顶点只提供单位 quad 的 aPos，
// 真正的方块世界坐标靠 aPacked + sectionBases[gl_DrawID] 在 shader 内还原；
// 这边所有几何都已经烘焙进顶点缓冲，所以完全不认识"section/方块/面"。

layout(location = 0) in vec3 aPos;       // section 局部坐标，范围 0..16
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in uint aMeta;      // type(8) | layer(8) | face(4) | flags(4)

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uSectionBase;               // (chunkX*16, sectionY*16, chunkZ*16)

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
flat out int vBlockType;
flat out int vFaceIndex;
flat out int vTextureLayer;
flat out int vFlags;

void main() {
    vec3 worldPos = uSectionBase + aPos;

    vWorldPos     = worldPos;
    vNormal       = aNormal;
    vTexCoord     = aTexCoord;
    vBlockType    = int( aMeta        & 0xFFu);
    vTextureLayer = int((aMeta >> 8)  & 0xFFu);
    vFaceIndex    = int((aMeta >> 16) & 0xFu);
    vFlags        = int((aMeta >> 20) & 0xFu);

    gl_Position = uProjection * uView * vec4(worldPos, 1.0);
}
