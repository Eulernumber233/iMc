#version 430 core

in vec2 vTexCoord;
out vec4 FragColor;

// G-Buffer 纹理
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gProperties;
uniform sampler2D ssao;

// 光照参数
uniform vec3 sunShineAmbient;
uniform vec3 sunShineDiffuse;
uniform vec3 sunShineDir;
uniform vec3 sunShinePos;
uniform float sunShineIntensity; // 0=夜晚, 1=白天，地平线附近平滑过渡

// 阴影贴图参数
uniform sampler2D varianceShadowMap;
uniform float sunShineNear;
uniform float sunShineFar;
uniform int SHADOW_WIDTH;
uniform vec3 uViewPos;
uniform mat4 lightSpaceMatrix;

// ---- PCSS+VSSM 常量（在光源空间归一化深度 [0,1] 下） ----
// currentDepth、moments.x 全部是 projCoords.z（正交投影下线性），统一空间做比较
const float MIN_FILTER_TEXELS = 1.5;   // 最小过滤半径（纹素）
const float MAX_FILTER_TEXELS = 12.0;  // 最大过滤半径（纹素）
const float LIGHT_SIZE_UV     = 0.008; // 光源"大小"（相对阴影贴图 UV），决定半影强度
const float LIGHT_BLEED_REDUCE = 0.2;  // 光渗抑制强度（过大会让接触阴影发灰）
// 方差下限要比单个方块在归一化深度下的跨度小
// 范围 farP-nearP ~ 400m，单方块 ~1m 对应 δ=0.0025，δ² ~ 6e-6
const float MIN_VARIANCE       = 1e-7;

// Poisson圆盘采样（16个样本）
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);

struct GBufferData {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    int blockType;
    float emissive;
    float roughness;
    float metallic;
};

GBufferData readGBuffer(vec2 texCoord) {
    GBufferData data;
    vec4 posData = texture(gPosition, texCoord);
    vec4 normalData = texture(gNormal, texCoord);
    vec4 albedoData = texture(gAlbedo, texCoord);
    vec4 propData = texture(gProperties, texCoord);

    data.position = posData.xyz;
    data.normal = normalize(normalData.xyz);
    data.albedo = albedoData.rgb;
    data.blockType = int(propData.r * 255.0);
    data.emissive = propData.g;
    data.roughness = propData.b;
    data.metallic = propData.a;
    return data;
}




// 基于法线与光线夹角的斜率 bias（在归一化线性深度空间下）
// 归一化深度范围 ~ 400m 被压到 [0,1]，1 个方块(1m) ≈ 0.0025 深度单位
// bias 必须小于方块厚度的一半，否则方块自身会遮住背面光照
float CalculateBias(float NdotL) {
    float slope = sqrt(clamp(1.0 - NdotL * NdotL, 0.0, 1.0)) / max(NdotL, 1e-3);
    return clamp(2e-4 * slope + 5e-5, 5e-5, 1e-3);
}

// VSSM 切比雪夫上界：返回"可见概率"
float ChebyshevUpperBound(vec2 moments, float currentDepth) {
    // 当前深度不大于均值 → 完全可见（大部分平坦表面走这条路径，极大减少噪声）
    if (currentDepth <= moments.x) {
        return 1.0;
    }

    float variance = moments.y - moments.x * moments.x;
    variance = max(variance, MIN_VARIANCE);

    float d = currentDepth - moments.x;
    float p = variance / (variance + d * d);

    // 光渗抑制：把 [LIGHT_BLEED_REDUCE, 1] 映射到 [0, 1]
    p = clamp((p - LIGHT_BLEED_REDUCE) / (1.0 - LIGHT_BLEED_REDUCE), 0.0, 1.0);
    return p;
}

// Interleaved Gradient Noise（Jimenez 2014）——基于屏幕坐标的低频抖动
float IGN(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// 使用少量点采样估计遮挡物平均深度（仅统计真正比 receiver 更近的样本）
// 不用 mipmap：mipmap 把邻近的几何深度混合进来，会让平坦表面被误判为有遮挡
float FindBlockerAvgDepth(vec2 uv, float currentDepth, float searchRadiusUV) {
    float sum = 0.0;
    int count = 0;
    // 8 个点足够粗略估计遮挡物深度
    for (int i = 0; i < 8; i++) {
        vec2 sUV = uv + poissonDisk[i] * searchRadiusUV;
        float d = texture(varianceShadowMap, sUV).r;
        // 只有明显比当前点更近的才算遮挡物
        if (d < currentDepth - 2e-4) {
            sum += d;
            count++;
        }
    }
    if (count == 0) return -1.0;
    return sum / float(count);
}

// PCF + VSSM 过滤：在给定半影半径内采样，对每个点做切比雪夫测试并平均
float PCSS_VSSM_Filter(vec2 uv, float filterRadiusUV, float currentDepth, vec2 rotUV) {
    // 低频抖动角度：每 4x4 屏幕像素共享同一角度（配合后续可加空间模糊）
    float angle = IGN(floor(rotUV * 0.25)) * 6.2831853;
    float ca = cos(angle), sa = sin(angle);

    // 打破 16 个固定方向的环形伪影
    mat2 rot = mat2(ca, -sa, sa, ca);

    float sum = 0.0;
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; i++) {
        vec2 offset = rot * poissonDisk[i] * filterRadiusUV;
        vec2 sUV = uv + offset;
        vec2 m = texture(varianceShadowMap, sUV).rg;
        sum += ChebyshevUpperBound(m, currentDepth);
    }
    return sum / float(SAMPLES);
}

// PCSS_VSSM：返回阴影量 [0,1]，0=无阴影，1=完全阴影
float ShadowCalculation_PCSS_VSSM(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir, vec2 screenUV) {
    // 1. 投影到 [0,1]^3
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // 2. 超出阴影贴图范围：无阴影（外部默认全光照）
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float NdotL = max(dot(normal, lightDir), 0.0);
    // 背面（面对光源背向）不应有"自阴影"，直接判定为阴影
    if (NdotL <= 0.0) {
        return 1.0;
    }

    float bias = CalculateBias(NdotL);
    float currentDepth = projCoords.z - bias;

    ivec2 smSize = textureSize(varianceShadowMap, 0);
    float texelUV = 1.0 / float(smSize.x);

    // 3. Blocker search：使用 mipmap 粗略估计遮挡物平均深度
    float blocker = FindBlockerAvgDepth(projCoords.xy, currentDepth, LIGHT_SIZE_UV);
    if (blocker < 0.0) {
        // 无遮挡 → 完全光照
        return 0.0;
    }

    // 4. 半影大小：PCSS 公式，(receiver - blocker) / blocker * lightSize
    //    深度已归一化到 [0,1]，LIGHT_SIZE_UV 也是 UV 空间，量纲一致
    float penumbraUV = (currentDepth - blocker) / max(blocker, 1e-4) * LIGHT_SIZE_UV;
    float filterRadiusUV = clamp(penumbraUV,
                                 MIN_FILTER_TEXELS * texelUV,
                                 MAX_FILTER_TEXELS * texelUV);

    // 5. 过滤
    float visibility = PCSS_VSSM_Filter(projCoords.xy, filterRadiusUV, currentDepth, screenUV);

    // 6. 远处柔化：接近阴影贴图边界时平滑过渡到无阴影，避免边界跳变
    vec2 fade = min(projCoords.xy, 1.0 - projCoords.xy);
    float edgeFade = smoothstep(0.0, 0.05, min(fade.x, fade.y));
    float zFade = 1.0 - smoothstep(0.9, 1.0, projCoords.z);
    visibility = mix(1.0, visibility, edgeFade * zFade);

    return 1.0 - visibility;
}

void main() {
    GBufferData data = readGBuffer(vTexCoord);
    if (data.blockType == 0) discard;

    vec3 dirLightDir = normalize(-sunShineDir);
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    // 夜晚（sunShineIntensity=0）完全跳过阴影计算，避免退化矩阵产生异常
    float dirShadow = 0.0;
    if (sunShineIntensity > 0.001) {
        vec4 fragPosLightSpace = lightSpaceMatrix * vec4(data.position, 1.0);
        dirShadow = ShadowCalculation_PCSS_VSSM(fragPosLightSpace, data.normal, dirLightDir, gl_FragCoord.xy);
    }
    // 阳光贡献按白天强度整体缩放；夜晚直接归零
    vec3 dirLightResult = (1.0 - dirShadow) * dirDiffuse * sunShineIntensity;

    float ao = texture(ssao, vTexCoord).r;
    vec3 ambient = sunShineAmbient * data.albedo * ao;
    vec3 result = ambient + dirLightResult;

    // test ao
    // result = vec3(ao);

    FragColor = vec4(result, 1.0);
}