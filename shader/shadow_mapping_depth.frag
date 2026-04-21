#version 330 core

out vec2 FragColor;
flat in int vBlockType;

uniform float dir_near;
uniform float dir_far;

const int BLOCK_ERRER = 255;

// 光源空间线性深度，归一化到 [0,1]
// gl_FragCoord.z 来自正交投影后 [0,1] 的 NDC 深度，对正交投影而言本身就是线性的
// 因此直接使用 gl_FragCoord.z 作为统一的深度度量

void main() {
    if (vBlockType == BLOCK_ERRER) discard;

    float d = gl_FragCoord.z; // [0,1] 线性（正交投影下）

    // 存储深度均值 m1 和深度平方均值 m2
    FragColor = vec2(d, d * d);
}
