#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 TexCoords;

uniform sampler2DArray blockTextures; // 方块纹理数组（与地形共用）
uniform vec3 lightDir;                 // 世界/相机空间光方向（从光源指向场景）

void main()
{
    vec4 tex = texture(blockTextures, TexCoords);
    if (tex.a < 0.5) discard;   // 保留镂空方块（如树叶）的透明

    // 简单方向光 + 环境底光，让立方体三个可见面有明暗区分
    vec3 N = normalize(Normal);
    float diff = max(dot(N, normalize(-lightDir)), 0.0);
    float lit = 0.5 + 0.5 * diff;
    FragColor = vec4(tex.rgb * lit, 1.0);
}
