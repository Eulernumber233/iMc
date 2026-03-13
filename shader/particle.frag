#version 430 core

in vec2 TexCoord;
in vec4 ParticleColor;
in float LifeFactor;

uniform int useTexture;
uniform sampler2DArray particleTextureArray;
uniform int textureLayer;

out vec4 FragColor;

void main() {
    vec4 color = ParticleColor;

    if (useTexture == 1 && textureLayer >= 0) {
        vec4 texColor = texture(particleTextureArray, vec3(TexCoord, textureLayer));
        color *= texColor;
    }

    // 根据生命周期调整alpha（可选）
    color.a *= smoothstep(0.0, 0.2, LifeFactor); // 淡入
    color.a *= smoothstep(1.0, 0.8, LifeFactor); // 淡出

    // 丢弃透明像素
    //if (color.a < 0.01) discard;

    FragColor = color;
    FragColor = vec4(1.0,1.0,0.0,1.0);//test
}