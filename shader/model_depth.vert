#version 330 core
// 模型深度着色器（顶点着色器）
// 用于将人物模型渲染到 CSM 级联阴影贴图。
// 接受与 mode.vert 相同的 aPos 输入和 model 矩阵，
// 配合 drawPosed 的骨骼动画变换写入光空间深度。

layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main()
{
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
