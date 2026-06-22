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

// 端面纹理层查表（CPU 端 BlockFaceType::end_layer_by_type 上传过来）：
// 每种 BlockType 一个层索引。仅带轴方块横躺时（主轴 X/Z）才会取到这里的值；
// 其他情况一律用 aTextureLayer（CPU 端按 face 查的"侧面/默认面"层）。
uniform int uEndLayerLookup[256];

// axis 编号：0=X, 1=Z, 2=Y。BlockFace / BlockOrient 0..5 → axis = idx>>1。
// kVAxis[face] 给出该面在世界坐标系中 V 轴对应哪个 axis：
//   FRONT/BACK/RIGHT/LEFT (0..3) 的 V = -Y → axis Y (2)
//   UP/DOWN (4,5)               的 V = ±Z → axis Z (1)
const int kVAxis[6] = int[6](2, 2, 2, 2, 1, 1);

// 该面是不是"端面"（与方块主轴同轴的面）。带轴方块横躺时此处贴端面图。
bool isEndFace(int face, int orient) {
    if (orient == 0xF) return false;     // ORIENT_NONE
    return (face >> 1) == (orient >> 1);
}

// 侧面 UV 是否需要绕中心旋转 90°。规则：
//   - 主轴 Y 或 ORIENT_NONE：方块按默认竖直放置，不转。
//   - 主轴 X/Z 的端面：用端面图（同心圆环），转不转无视觉差异 → 不转。
//   - 主轴 X/Z 的侧面：当该面的 V 轴对应的世界 axis 与主轴**不一致**时才转 90°，
//     让木纹方向永远顺着主轴。例：原木沿 Z 横躺时 UP/DOWN 面 V 轴本来就是 Z，无需转。
bool shouldRotateUV(int face, int orient) {
    if (orient == 0xF) return false;
    int orientAxis = orient >> 1;
    if (orientAxis == 2) return false;             // 主轴 Y
    if ((face >> 1) == orientAxis) return false;   // 端面
    return kVAxis[face] != orientAxis;             // V 轴已对齐主轴 → 不转
}

void main() {
    // 解包局部坐标 + 面索引 + 朝向
    int lx     = int( aPacked        & 0xFu);
    int ly     = int((aPacked >> 4)  & 0xFu);
    int lz     = int((aPacked >> 8)  & 0xFu);
    int face   = int((aPacked >> 12) & 0x7u);
    int orient = int((aPacked >> 15) & 0xFu);

    // 还原方块中心世界坐标：sectionBase + 局部 + 0.5
    vec3 sectionBase = sectionBases[gl_DrawID].xyz;
    vec3 blockPos = sectionBase + vec3(float(lx), float(ly), float(lz)) + vec3(0.5);

    mat4 faceMatrix = faceMatrices[face];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(blockPos, 1.0);
    mat4 model = translation * faceMatrix;

    vWorldPos = (model * vec4(aPos, 1.0)).xyz;
    vNormal = mat3(faceMatrix) * aNormal;

    // 按朝向旋转 UV：横躺原木的侧面木纹要顺着主轴 X / Z 而非默认竖直。
    vec2 uv = aTexCoord;
    if (shouldRotateUV(face, orient)) {
        // 绕 (0.5, 0.5) 旋转 90°：x' = y, y' = 1 - x
        uv = vec2(uv.y, 1.0 - uv.x);
    }
    vTexCoord = uv;

    vBlockType = int(aBlockType);
    vFaceIndex = face;

    // 端面取 endLayer，其他取 aTextureLayer。endLayer < 0 视为未注册（保底）。
    int layer = int(aTextureLayer);
    if (isEndFace(face, orient)) {
        int endLayer = uEndLayerLookup[int(aBlockType)];
        if (endLayer >= 0) layer = endLayer;
    }
    vTextureLayer = layer;

    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
