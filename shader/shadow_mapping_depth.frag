#version 330 core

out vec2 FragColor;
flat in int vBlockType;

uniform float dir_near;
uniform float dir_far;

const int BLOCK_ERRER = 255;

float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // NDC[-1,1]
    return (2.0 * dir_near * dir_far) / (dir_far + dir_near - z * (dir_far - dir_near));
}

void main() {
    if (vBlockType == BLOCK_ERRER) discard;

    float depth = gl_FragCoord.z;
    float linear_depth = LinearizeDepth(depth); // 转为线性深度（避免非线性误差）
    
    // 存储深度均值m1和深度平方均值m2
    FragColor = vec2(linear_depth, linear_depth * linear_depth + 1e-6);
}