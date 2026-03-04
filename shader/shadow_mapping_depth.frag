#version 330 core

out vec2 FragColor;
flat in int vBlockType;

uniform float dir_near;
uniform float dir_far;

const int BLOCK_ERRER = 255;

void main() {
    if (vBlockType == BLOCK_ERRER) discard;

    // 正交投影下 gl_FragCoord.z 已经是线性深度（范围 [0,1]）
    // 转换为实际线性深度： depth = near + (far - near) * z
    float depth_linear = dir_near + (dir_far - dir_near) * gl_FragCoord.z;

    // 存储一阶矩（深度）和二阶矩（深度平方）
    FragColor = vec2(depth_linear, depth_linear * depth_linear + 1e-6);
}