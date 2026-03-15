#version 430 core

// 输入顶点属性
layout(location = 0) in vec2 aPos;          // 粒子面片的本地坐标（-0.5~0.5 或 -1~1）
layout(location = 1) in vec2 aTexCoord;    // 纹理坐标
layout(location = 2) in vec4 aParticlePos; // xyz: 世界空间位置, w: 剩余生命周期
layout(location = 3) in vec4 aParticleVel; // xyz: 速度, w: 粒子自定义尺寸（仅usePerParticleSize生效时用）
layout(location = 4) in vec4 aParticleColor;// 粒子自定义颜色（可选，配合usePerParticleColor）

// 统一变量
uniform mat4 view;               // 视图矩阵（世界→观察空间）
uniform mat4 projection;         // 投影矩阵（观察→裁剪空间）
uniform vec4 colorStart;         // 生命周期开始颜色
uniform vec4 colorEnd;           // 生命周期结束颜色
uniform float sizeStart;         // 初始尺寸
uniform float sizeEnd;           // 结束尺寸
uniform float maxLifetime;       // 最大生命周期
uniform int usePerParticleColor; // 是否使用粒子自定义颜色（0/1）
uniform int usePerParticleSize;  // 是否使用粒子自定义尺寸（0/1）

// 输出到片段着色器
out vec2 TexCoord;
out vec4 ParticleColor;
out float LifeFactor;

void main() {
    // 生命周期因子
    float life = aParticlePos.w;
    LifeFactor = clamp(life / maxLifetime, 0.0, 1.0);

    // 计算粒子尺寸
    float size;
    if (usePerParticleSize == 1) {
        size = aParticleVel.w; // 使用粒子自定义尺寸
    } else {
        size = mix(sizeEnd, sizeStart, LifeFactor);
    }

    // 粒子颜色
    if (usePerParticleColor == 1) {
        ParticleColor = aParticleColor; // 使用粒子自定义颜色
    } else {
        ParticleColor = mix(colorEnd, colorStart, LifeFactor);
    }

    // 在观察空间构建面向相机的面片
    // 将粒子中心位置转换到观察空间
    vec4 particleViewPos = view * vec4(aParticlePos.xyz, 1.0);

    // 对于屏幕对齐的广告牌，在观察空间中使用观察空间的基向量
    // 观察空间中：X轴向右(1,0,0)，Y轴向上(0,1,0)，Z轴向屏幕内(0,0,-1)
    vec3 right = vec3(1.0, 0.0, 0.0);   // 观察空间右向量
    vec3 up = vec3(0.0, 1.0, 0.0);      // 观察空间上向量

    // 观察空间中的顶点位置
    vec3 vertexViewPos = particleViewPos.xyz + (right * aPos.x * size)+ (up * aPos.y * size);
    
    // test 
    //vec3 worldpos = aParticlePos.xyz + (right * aPos.x * size)*5.0f + (up * aPos.y * size)*5.0f;
    //vertexViewPos = (view * vec4(worldpos, 1.0)).xyz;

    //vertexViewPos = particleViewPos.xyz;
    // 转换到裁剪空间
    gl_Position = projection * vec4(vertexViewPos, 1.0);

    // 纹理坐标
    TexCoord = aTexCoord;
}