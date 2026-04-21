#version 330 core
in vec2 TexCoords;
out float fragColor;

uniform sampler2D ssaoInput;

// 4x4 box blur：匹配 4x4 噪声纹理的 tiling 周期
// 此处必须覆盖整个噪声周期，否则残留的噪声图案会形成三角形/条纹伪影
void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;

    // 采样 [-2, -1, 0, 1] × [-2, -1, 0, 1] = 16 个点（4×4 均匀权重）
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
        }
    }
    fragColor = result / 16.0;
}
