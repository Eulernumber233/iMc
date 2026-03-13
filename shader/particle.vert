#version 430 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

// 实例数据
layout(location = 2) in vec4 aParticlePos;   // xyz: position, w: lifetime
layout(location = 3) in vec4 aParticleVel;   // xyz: velocity, w: size
layout(location = 4) in vec4 aParticleColor; // 每个粒子的颜色（如果usePerParticleColor为1）

uniform mat4 view;
uniform mat4 projection;
uniform vec4 colorStart;
uniform vec4 colorEnd;
uniform float sizeStart;
uniform float sizeEnd;
uniform float maxLifetime;
uniform int usePerParticleColor; // 0: 使用uniform颜色渐变, 1: 使用每个粒子的颜色
uniform int usePerParticleSize;  // 0: 使用uniform大小渐变, 1: 使用每个粒子的大小（velocity.w）

out vec2 TexCoord;
out vec4 ParticleColor;
out float LifeFactor;

void main() {
    // 计算生命周期因子（0到1）
    // maxLifetime从uniform传递
    float life = aParticlePos.w;
    LifeFactor = clamp(life / maxLifetime, 0.0, 1.0);

    // 选择大小：如果使用每个粒子大小，则使用velocity.w，否则使用uniform渐变
    float size;
    if (usePerParticleSize == 1) {
        size = aParticleVel.w;
    } else {
        size = mix(sizeEnd, sizeStart, LifeFactor);
    }
    // 选择颜色：如果使用每个粒子颜色，则使用aParticleColor，否则使用uniform渐变
    if (usePerParticleColor == 1) {
        ParticleColor = aParticleColor;
    } else {
        ParticleColor = mix(colorEnd, colorStart, LifeFactor);
    }

    // 提取粒子世界位置
    vec3 worldPos = aParticlePos.xyz;

    // 广告牌计算：将四边形旋转以面向相机
    // 从视图矩阵中提取右向量和上向量
    mat4 viewInv = inverse(view);
    vec3 right = vec3(viewInv[0][0], viewInv[1][0], viewInv[2][0]);
    vec3 up = vec3(viewInv[0][1], viewInv[1][1], viewInv[2][1]);

    // 计算最终顶点位置
    vec3 pos = worldPos + right * aPos.x * size + up * aPos.y * size;

    gl_Position = projection * view * vec4(pos, 1.0);
    TexCoord = aTexCoord;
}