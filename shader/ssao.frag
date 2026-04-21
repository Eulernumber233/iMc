#version 330 core
out float FragColor;
in vec2 TexCoords;

// G-Buffer 纹理
uniform sampler2D gPositionDepth;   // 世界空间位置 (xyz)
uniform sampler2D gNormal;           // 世界空间法线
uniform sampler2D texNoise;           // 随机旋转纹理（16x16）

// SSAO 参数（建议作为 uniform 以便实时调整）
uniform vec3 samples[64];             // 采样核（切线空间）
uniform int kernelSize = 64;
uniform float radius = 0.8;            // 采样半径（方块世界尺度，1 方块约 1m）
uniform float bias = 0.08;             // 基础 bias（掠射角下会动态放大）

// 屏幕尺寸（用于噪声纹理缩放）
uniform vec2 screenSize;

// 视图变换矩阵
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // 1. 从 G-Buffer 获取世界空间位置和法线
    vec3 fragPosWorld = texture(gPositionDepth, TexCoords).xyz;
    vec3 normalWorld = texture(gNormal, TexCoords).rgb;

    // 2. 转换到视图空间
    vec3 fragPos = (view * vec4(fragPosWorld, 1.0)).xyz;          // 视图空间位置（z 为负）
    vec3 normal = normalize(mat3(view) * normalWorld);            // 视图空间法线
    float fragDepth = -fragPos.z;                                  // 正深度（距离）

    // 掠射角检测：视线 V 指向相机（view space 里相机在原点 → V = normalize(-fragPos)）
    // NdotV 小（接近 0）意味着视线几乎平行于表面，此时 SSAO 极不稳定
    vec3 V = normalize(-fragPos);
    float NdotV = max(dot(normal, V), 0.0);
    // 早退：视线背向表面（G-Buffer 精度问题常产生 NdotV<=0），直接无遮挡
    if (NdotV < 0.05) {
        FragColor = 1.0;
        return;
    }
    // 动态 bias：掠射角（NdotV 小）时放大到原始的 3~4 倍
    float dynamicBias = bias * (1.0 + 3.0 * (1.0 - NdotV));

    // 3. 获取随机旋转向量，并构建 TBN 矩阵（切线空间 -> 视图空间）
    //    噪声纹理尺寸为 4x4，因此缩放因子为 screenSize / 4.0
    vec2 noiseScale = screenSize / 4.0;
    vec3 randomVec = texture(texNoise, TexCoords * noiseScale).xyz;

    // 构建 tangent 和 bitangent
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    // 处理退化情况（随机向量与法线平行）
    if (length(tangent) < 0.1) {
        tangent = vec3(1.0, 0.0, 0.0);
        tangent = normalize(tangent - normal * dot(tangent, normal));
    }
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // 4. 遍历采样核，累计遮挡
    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // 将采样点从切线空间变换到视图空间
        vec3 sampleOffset = TBN * samples[i];
        vec3 samplePos = fragPos + sampleOffset * radius;          // 视图空间位置

        // 投影采样点，获取其在屏幕空间的纹理坐标
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;                          // 映射到 [0,1]

        // 边界检查
        if (any(lessThan(offset.xy, vec2(0.0))) || any(greaterThan(offset.xy, vec2(1.0))))
            continue;

        // 从 G-Buffer 读取采样点对应的世界空间位置，并转换到视图空间
        vec3 sampleWorldPos = texture(gPositionDepth, offset.xy).xyz;
        float sampleDepthView = (view * vec4(sampleWorldPos, 1.0)).z;
        float sampleDepth = -sampleDepthView;                       // 采样点的正深度

        // 遮挡判断：使用动态 bias（掠射角下更大）
        float occlude = (sampleDepth < (-samplePos.z - dynamicBias)) ? 1.0 : 0.0;

        // 范围检查 1：传统的 radius 内 smoothstep，半径外衰减
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragDepth - sampleDepth));

        // 范围检查 2：深度差过大时认为是跨越到了不相关表面（比如掠射角下
        // 屏幕上相邻像素其实对应远处的方块），直接丢弃该样本
        // 阈值设为 radius 的 2 倍：正常遮挡物不会超过这个差距
        float depthDelta = abs(fragDepth - sampleDepth);
        float validSample = step(depthDelta, radius * 2.0);

        occlusion += occlude * rangeCheck * validSample;
    }

    // 5. 计算最终环境光遮蔽因子（1 - 平均遮挡率）
    occlusion = 1.0 - (occlusion / float(kernelSize));
    FragColor = occlusion;
}