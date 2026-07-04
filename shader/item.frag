#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec2 TexCoords;

uniform sampler2D texture_diffuse1; // 物品图标
uniform vec3 lightDir;              // 世界空间光方向（从光源指向场景）

void main()
{
    vec4 tex = texture(texture_diffuse1, TexCoords);
    if (tex.a < 0.5) discard;   // alpha 挖空出轮廓

    // 简单方向光 + 环境底光，让挤出的侧壁有立体感（避免全黑）
    vec3 N = normalize(Normal);
    float diff = max(dot(N, normalize(-lightDir)), 0.0);
    float lit = 0.55 + 0.45 * diff;
    FragColor = vec4(tex.rgb * lit, 1.0);
}
