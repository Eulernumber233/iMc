#version 430 core

const int FACEFRONT = 2;
const int FACEBACK = 3;
const int FACERIGHT = 0;
const int FACELEFT = 1;
const int FACEUP = 4;
const int FACEDOWN = 5;

const int BLOCK_ERRER = 255; // 错误方块类型
const int BLOCK_AIR = 0;
const int BLOCK_STONE = 1;
const int BLOCK_DIRT = 2;
const int BLOCK_GRASS = 3;
const int BLOCK_WATER = 4;
const int BLOCK_SAND = 5;
const int BLOCK_WOOD = 6;
const int BLOCK_LEAVES = 7;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
flat in int vBlockType;
flat in int vFaceIndex;
flat in int vTextureLayer;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gProperties;

uniform sampler2DArray uTextureArray;

const float NEAR = 0.1;
const float FAR = 1000.0;

float LinearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * NEAR * FAR) / (FAR + NEAR - z * (FAR - NEAR));
}

void main() {
    if (vBlockType == BLOCK_ERRER) discard;
    gPosition = vec4(vWorldPos, LinearizeDepth(gl_FragCoord.z));
    gNormal = vec4(normalize(vNormal), 0.0);

    // 从纹理数组采样，层索引由 vTextureLayer 指定
    gAlbedo = texture(uTextureArray, vec3(vTexCoord, vTextureLayer));

    // 草方块顶面特殊颜色调制（示例：调亮）
    if (vBlockType == BLOCK_GRASS && vFaceIndex == FACEUP) {
        // 这里可以用 uniform 传递调色参数，此处演示固定值
        gAlbedo.rgb *= vec3(140.0, 230.0, 60.0) / 255.0;
    }

    // 属性：方块类型（0-255）、自发光强度、粗糙度、金属度
    gProperties = vec4(
        float(vBlockType) / 255.0,  // 方块类型（归一化到0-1）
        0.0,                        // 自发光强度（大部分方块不发光）
        0.8,                        // 粗糙度（大部分方块是粗糙的）
        0.0                         // 金属度（方块不是金属）
    );
}