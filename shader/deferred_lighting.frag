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
const float BIAS_COEFF = 0.00015;
const float MIN_FILTER_SIZE = 3.0;
const float MAX_FILTER_SIZE = 25.0;
const float LIGHT_SIZE = 0.15;

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




float LinearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * sunShineNear * sunShineFar) / (sunShineFar + sunShineNear - z * (sunShineFar - sunShineNear));
}

// 改进的bias计算，防止自阴影
float CalculateBias(vec3 normal, vec3 lightDir, float NdotL) {
    float bias = BIAS_COEFF * tan(acos(clamp(NdotL, 0.0, 1.0)));
    bias = clamp(bias, BIAS_COEFF * 0.01, BIAS_COEFF * 0.2);
    return bias;
}

// VSSM核心计算
float ChebyshevUpperBound(vec2 moments, float currentDepth) {
    // 从方差贴图中提取均值和平方均值
    float mean = moments.x;
    float mean2 = moments.y;
    
    // 计算方差
    float variance = mean2 - (mean * mean);
    variance = max(variance, 0.0);
    
    // 避免除零
    if (variance < 0.000001) {
        return currentDepth <= mean ? 1.0 : 0.0;
    }
    
    // 切比雪夫不等式计算
    float d = currentDepth - mean;
    if (d <= 0.0) {
        return 1.0; // 当前深度小于等于平均深度，完全可见
    }
    
    float p = variance / (variance + d * d);
    
    // 增强对比度，防止光渗
    float lightBleedingReduction = 0.2; // 控制光渗抑制程度
    p = clamp((p - lightBleedingReduction) / (1.0 - lightBleedingReduction), 0.0, 1.0);
    
    return p;
}

// 遮挡物搜索
float FindBlockerDepth_VSSM(vec3 projCoords, float currentDepth) {
    // 先用Mipmap快速检查
    vec2 mipMoments = textureLod(varianceShadowMap, projCoords.xy, 1.0).rg;
    float mipMean = mipMoments.r;
    
    // 快速排除：如果没有遮挡迹象
    if (mipMean <= 0.001 || currentDepth <= mipMean + 0.03) {
        return 0.0;
    }
    
    // 根据深度差异决定采样策略
    float depthDiff = currentDepth - mipMean;
    
    if (depthDiff < 0.1) {
        // 差异小，可能没有遮挡或遮挡很弱
        // 采样少数点确认
        int found = 0;
        float sum = 0.0;
        
        for (int i = 0; i < 8; i++) {
            vec2 sampleUV = projCoords.xy + poissonDisk[i] * (1.0/SHADOW_WIDTH);
            float depth = texture(varianceShadowMap, sampleUV).r;
            if (depth > 0.001 && currentDepth > depth + 0.01) {
                sum += depth;
                found++;
            }
        }
        
        return found > 0 ? sum / float(found) : 0.0;
    } else {
        // 差异大，很可能有遮挡
        // 直接使用Mipmap均值作为估计
        return mipMean;
    }
}

// 半影计算
float CalculatePenumbraSize(float avgBlockerDepth, float currentDepth, vec3 projCoords) {
    // 如果没有遮挡物，返回最小过滤核
    if (avgBlockerDepth <= 0.001) {
        return MIN_FILTER_SIZE;
    }
    
    // 计算世界空间中的半影大小
    // PCSS公式: (光源大小 * (当前深度 - 遮挡物深度)) / 遮挡物深度
    float worldPenumbra = (LIGHT_SIZE * (currentDepth - avgBlockerDepth)) / avgBlockerDepth;
    
    // 将世界空间单位转换为纹理空间单位
    // 正交投影下，整个深度范围映射到纹理宽度
    float worldToTex = float(SHADOW_WIDTH) / (sunShineFar - sunShineNear);
    float texPenumbra = worldPenumbra * worldToTex;
    
    // 根据深度差动态调整
    float depthRatio = (currentDepth - avgBlockerDepth) / currentDepth;
    texPenumbra *= 1.0 + depthRatio * 2.0;
    
    // 限制过滤核大小
    return clamp(texPenumbra, MIN_FILTER_SIZE, MAX_FILTER_SIZE);
}

// 改进的PCF过滤
float PCSS_VSSM_Filter(vec3 projCoords, float filterSize, float currentDepth, float bias) {
    // 计算纹理像素大小和模糊半径
    vec2 texelSize = 1.0 / textureSize(varianceShadowMap, 0);
    float radius = filterSize;
    
    float shadow = 0.0;
    int sampleCount = 0;
    
    // 使用旋转的Poisson圆盘采样
    float randomAngle = fract(sin(dot(projCoords.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.283185;
    mat2 rotation = mat2(cos(randomAngle), -sin(randomAngle),
                         sin(randomAngle), cos(randomAngle));
    
    // 根据过滤核大小调整采样数量
    int samples = int(clamp(radius * 2.0, 4.0, 16.0));
    
    for (int i = 0; i < samples; i++) {
        // 旋转采样点
        vec2 offset = rotation * poissonDisk[i % 16] * radius * texelSize;
        vec2 sampleUV = projCoords.xy + offset;
        
        // 边界检查
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }
        
        // 采样方差贴图
        vec2 moments = texture(varianceShadowMap, sampleUV).rg;
        
        // 使用切比雪夫计算可见性
        shadow += ChebyshevUpperBound(moments, currentDepth - bias);
        sampleCount++;
    }
    
    // 处理无效采样
    if (sampleCount == 0) {
        vec2 moments = texture(varianceShadowMap, projCoords.xy).rg;
        return ChebyshevUpperBound(moments, currentDepth - bias);
    }
    
    return shadow / float(sampleCount);
}

// PCSS_VSSM 阴影计算函数
float ShadowCalculation_PCSS_VSSM(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // 1. 投影坐标转换
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // 2. 边界检查
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    // 计算线性深度和bias
    float currentDepth = LinearizeDepth(projCoords.z);
    float NdotL = max(dot(normal, lightDir), 0.0);
    float bias = CalculateBias(normal, lightDir, NdotL);
    
    // 查找遮挡物
    float blockerDepth = FindBlockerDepth_VSSM(projCoords, currentDepth);
    
    // 计算半影大小
    float filterSize = CalculatePenumbraSize(blockerDepth, currentDepth, projCoords);
    
    // 执行过滤
    float visibility = PCSS_VSSM_Filter(projCoords, filterSize, currentDepth, bias);
    
    // 最终阴影计算
    // 应用深度衰减，使远处的阴影更柔和
    float depthFactor = clamp(currentDepth / sunShineFar, 0.0, 1.0);
    visibility = mix(visibility, 1.0, depthFactor * 0.3);
    
    // 根据法线和光方向微调
    visibility = mix(visibility, 1.0, (1.0 - NdotL) * 0.1);
    
    return 1.0 - visibility;
}

void main() {
    GBufferData data = readGBuffer(vTexCoord);
    if (data.blockType == 0) discard;

    vec3 dirLightDir = normalize(-sunShineDir);
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(data.position, 1.0);
    float dirShadow = ShadowCalculation_PCSS_VSSM(fragPosLightSpace, data.normal, sunShineDir);
    vec3 dirLightResult = (1.0 - dirShadow) * dirDiffuse;

    float ao = texture(ssao, vTexCoord).r;
    vec3 ambient = sunShineAmbient * data.albedo * ao;
    vec3 result = ambient + dirLightResult;
    FragColor = vec4(result, 1.0);
}