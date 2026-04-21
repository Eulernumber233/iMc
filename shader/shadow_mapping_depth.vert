#version 330 core

uniform mat4 lightSpaceMatrix;
layout(location = 0) in vec3 aPos;
layout(location = 5) in vec3 aBlockPos;      // 实例位置
layout(location = 6) in int aFaceIndex;      // 面索引
layout(location = 7) in int aBlockType;      // 方块类型

flat out int vBlockType;

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
    vBlockType = aBlockType;

    mat4 faceMatrix = faceMatrices[aFaceIndex];
    mat4 translation = mat4(1.0);
    translation[3] = vec4(aBlockPos, 1.0);
    mat4 model = translation * faceMatrix;
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}