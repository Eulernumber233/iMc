#version 460 core

// 烘焙路径的阴影深度顶点着色器：只需要位置。
uniform mat4 lightSpaceMatrix;
uniform vec3 uSectionBase;

layout(location = 0) in vec3 aPos;   // section 局部坐标

void main() {
    gl_Position = lightSpaceMatrix * vec4(uSectionBase + aPos, 1.0);
}
