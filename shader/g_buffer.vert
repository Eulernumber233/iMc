#version 430 core

// 输入：单位正方形的顶点
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 5) in mat4 aModelMatrix;// 实例化数据：模型矩阵和方块类型

// 输出
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

// 统一变量
uniform mat4 uView;
uniform mat4 uProjection;


void main() {
    // 计算世界空间位置
    vWorldPos = vec3(aModelMatrix * vec4(aPos, 1.0));
    
    // 计算世界空间法线（忽略缩放）
    vNormal = normalize(mat3(transpose(inverse(aModelMatrix))) * aNormal);
    
    // 传递纹理坐标
    vTexCoord = aTexCoord;
    
    // 计算裁剪空间位置
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}