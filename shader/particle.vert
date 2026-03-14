#version 430 core

// 输入顶点属性
layout(location = 0) in vec2 aPos;          // 粒子面片的本地坐标（-0.5~0.5 或 -1~1）
layout(location = 1) in vec2 aTexCoord;    // 纹理坐标
layout(location = 2) in vec4 aParticlePos; // xyz: 世界空间位置, w: 剩余生命周期
layout(location = 3) in vec4 aParticleVel; // xyz: 速度, w: 粒子自定义尺寸（仅usePerParticleSize生效时用）
layout(location = 4) in vec4 aParticleColor;// 新增：粒子自定义颜色（可选，配合usePerParticleColor）

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
    // 1. 计算生命周期因子（0=死亡，1=新生）
    float life = aParticlePos.w;
    LifeFactor = clamp(life / maxLifetime, 0.0, 1.0);

    // 2. 计算粒子尺寸（支持全局尺寸插值/粒子自定义尺寸）
    float size;
    if (usePerParticleSize == 1) {
        size = aParticleVel.w; // 使用粒子自定义尺寸
    } else {
        // mix(a, b, t) = a*(1-t) + b*t → 新生用sizeStart，死亡用sizeEnd
        size = mix(sizeEnd, sizeStart, LifeFactor);
    }

    // 3. 计算粒子颜色（支持全局颜色插值/粒子自定义颜色）
    if (usePerParticleColor == 1) {
        ParticleColor = aParticleColor; // 使用粒子自定义颜色
    } else {
        // 新生用colorStart，死亡用colorEnd
        ParticleColor = mix(colorEnd, colorStart, LifeFactor);
    }

    // 4. 公告板核心：在观察空间构建面向相机的面片
    // 步骤1：将粒子中心位置转换到观察空间
    vec4 particleViewPos = view * vec4(aParticlePos.xyz, 1.0);
    
    // 步骤2：观察空间中，相机的右向量=X轴(1,0,0)，上向量=Y轴(0,1,0)
    // （观察空间特性：相机朝向-Z轴，X=右，Y=上，天然面向相机）
    vec3 right = vec3(1.0, 0.0, 0.0);   // 观察空间右向量
    vec3 up = vec3(0.0, 1.0, 0.0);      // 观察空间上向量
    
    // 步骤3：计算观察空间中的顶点位置（aPos建议范围：-0.5~0.5，适配尺寸逻辑）
    vec3 vertexViewPos = particleViewPos.xyz + (right * aPos.x * size) + (up * aPos.y * size);

    // 步骤4：转换到裁剪空间（投影矩阵×观察空间位置）
    gl_Position = projection * vec4(vertexViewPos, 1.0);

    // 5. 传递纹理坐标
    TexCoord = aTexCoord;
}