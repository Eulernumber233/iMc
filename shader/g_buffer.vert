#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 5) in vec3 aBlockPos;      // 实例位置
layout(location = 6) in int aFaceIndex;      // 面索引
layout(location = 7) in int aBlockType;      // 方块类型
layout(location = 8) in int aTextureLayer;   // 纹理层索引

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
flat out int vBlockType;
flat out int vFaceIndex;
flat out int vTextureLayer;

uniform mat4 uView;
uniform mat4 uProjection;

const int FACERIGHT = 0;
const int FACELeft = 1;
const int FACEFRONT = 2;
const int FACEBACK = 3;
const int FACEUP = 4;
const int FACEDOWN = 5;

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
    mat4 faceMatrix = faceMatrices[aFaceIndex];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(aBlockPos, 1.0);
    mat4 model = translation * faceMatrix;

    vWorldPos = (model * vec4(aPos, 1.0)).xyz;
    vNormal = mat3(faceMatrix) * aNormal;   // 法线变换
    vTexCoord = aTexCoord;

    vBlockType = aBlockType;
    vFaceIndex = aFaceIndex;
    vTextureLayer = aTextureLayer;

    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}