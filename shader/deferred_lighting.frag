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

// 阴影贴图参数
uniform sampler2D varianceShadowMap;
uniform float sunShineNear;
uniform float sunShineFar;
uniform int SHADOW_WIDTH;
uniform vec3 uViewPos;
uniform mat4 lightSpaceMatrix;

// 常量
const float BIAS_COEFF = 0.0015;
const float MIN_FILTER_SIZE = 1.8;
const float MAX_FILTER_SIZE = 10.0;
const float LIGHT_SIZE = 0.03;

// 泊松圆盘采样点（16个）
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

// 从 G-Buffer 读取数据
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

// 简单的哈希函数，基于屏幕坐标生成随机数（与视角无关）
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// 计算阴影偏移量（bias）
float CalculateBias(vec3 normal, vec3 lightDir, float NdotL) {
    float bias = BIAS_COEFF * tan(acos(clamp(NdotL, 0.0, 1.0)));
    return clamp(bias, BIAS_COEFF * 0.01, BIAS_COEFF * 0.2);
}

// VSSM 切比雪夫上界
float ChebyshevUpperBound(vec2 moments, float currentDepth) {
    float mean = moments.x;
    float mean2 = moments.y;
    float variance = max(mean2 - mean * mean, 0.0);

    if (variance < 0.000001) {
        return currentDepth <= mean ? 1.0 : 0.0;
    }

    float d = currentDepth - mean;
    if (d <= 0.0) return 1.0;

    float p = variance / (variance + d * d);
    // 减少漏光
    float lightBleedingReduction = 0.2;
    p = clamp((p - lightBleedingReduction) / (1.0 - lightBleedingReduction), 0.0, 1.0);
    return p;
}

// 查找平均遮挡物深度（小区域采样，更稳定）
float FindBlockerDepth(vec3 projCoords, float currentDepth) {
    vec2 texelSize = 1.0 / textureSize(varianceShadowMap, 0);
    float searchRadius = 2.0; // 纹素单位

    float totalDepth = 0.0;
    int count = 0;

    for (int i = 0; i < 8; i++) {
        vec2 offset = poissonDisk[i] * searchRadius * texelSize;
        vec2 sampleUV = projCoords.xy + offset;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = texture(varianceShadowMap, sampleUV).r; // 一阶矩
        if (sampleDepth > 0.001 && sampleDepth < currentDepth - 0.01) {
            totalDepth += sampleDepth;
            count++;
        }
    }

    if (count == 0) return 0.0;
    return totalDepth / float(count);
}

// 计算半影大小（纹素单位）
float CalculatePenumbraSize(float avgBlockerDepth, float currentDepth) {
    if (avgBlockerDepth <= 0.001) return MIN_FILTER_SIZE;

    float worldPenumbra = (LIGHT_SIZE * (currentDepth - avgBlockerDepth)) / avgBlockerDepth;
    float worldToTex = float(SHADOW_WIDTH) / (sunShineFar - sunShineNear);
    float texPenumbra = worldPenumbra * worldToTex;

    return clamp(texPenumbra, MIN_FILTER_SIZE, MAX_FILTER_SIZE);
}

// PCF 滤波（固定16采样，随机旋转）
float PCSS_Filter(vec3 projCoords, float filterSize, float currentDepth, float bias) {
    vec2 texelSize = 1.0 / textureSize(varianceShadowMap, 0);
    float radius = filterSize;

    // 基于屏幕坐标的随机旋转（稳定无频闪）
    vec2 screenPos = gl_FragCoord.xy / vec2(textureSize(gPosition, 0));
    float randomAngle = hash(screenPos) * 2.0 * 3.14159265;
    mat2 rotation = mat2(cos(randomAngle), -sin(randomAngle),
                         sin(randomAngle), cos(randomAngle));

    float shadow = 0.0;
    int sampleCount = 0;
    const int SAMPLES = 16;

    for (int i = 0; i < SAMPLES; i++) {
        vec2 offset = rotation * poissonDisk[i] * radius * texelSize;
        vec2 sampleUV = projCoords.xy + offset;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        vec2 moments = texture(varianceShadowMap, sampleUV).rg;
        shadow += ChebyshevUpperBound(moments, currentDepth - bias);
        sampleCount++;
    }

    if (sampleCount == 0) {
        vec2 moments = texture(varianceShadowMap, projCoords.xy).rg;
        return ChebyshevUpperBound(moments, currentDepth - bias);
    }

    return shadow / float(sampleCount);
}

// 主阴影计算（PCSS + VSSM）
float ShadowCalculation_PCSS_VSSM(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // 1. 透视除法并转换到 [0,1] 范围
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // 2. 检查是否在光源视锥外
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    // 3. 重建当前片元的线性深度（与阴影贴图中存储的一致）
    float currentDepth = sunShineNear + (sunShineFar - sunShineNear) * projCoords.z;

    float NdotL = max(dot(normal, lightDir), 0.0);
    float bias = CalculateBias(normal, lightDir, NdotL);

    // 4. 查找平均遮挡物深度
    float avgBlockerDepth = FindBlockerDepth(projCoords, currentDepth);

    // 5. 计算半影大小
    float filterSize = CalculatePenumbraSize(avgBlockerDepth, currentDepth);

    // 6. 执行 PCF 滤波
    float visibility = PCSS_Filter(projCoords, filterSize, currentDepth, bias);

    // 7. 距离衰减和法线调制，减少远处伪影
    float depthFactor = clamp(currentDepth / sunShineFar, 0.0, 1.0);
    visibility = mix(visibility, 1.0, depthFactor * 0.3);
    visibility = mix(visibility, 1.0, (1.0 - NdotL) * 0.1);

    return 1.0 - visibility;
}

void main() {
    GBufferData data = readGBuffer(vTexCoord);

    if (data.blockType == 0) discard;

    // 平行光漫反射
    vec3 dirLightDir = normalize(-sunShineDir);
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    // 阴影计算
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(data.position, 1.0);
    float dirShadow = ShadowCalculation_PCSS_VSSM(fragPosLightSpace, data.normal, sunShineDir);
    vec3 dirLightResult = (1.0 - dirShadow) * dirDiffuse;

    // 环境光 + SSAO
    float ao = texture(ssao, vTexCoord).r;
    vec3 ambient = sunShineAmbient * data.albedo * ao;

    vec3 result = ambient + dirLightResult;
    FragColor = vec4(result, 1.0);
}
