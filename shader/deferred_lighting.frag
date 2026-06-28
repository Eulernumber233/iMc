#version 430 core

in vec2 vTexCoord;
out vec4 FragColor;

// G-Buffer 纹理
uniform sampler2D gDepth;       // 深度纹理（GL_DEPTH_COMPONENT32F），用于重建位置
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gProperties;
uniform sampler2D ssao;

// 从深度重建位置所需的逆矩阵
uniform mat4 invProjection;     // 深度 + 屏幕 UV → 视图空间
uniform mat4 invView;           // 视图空间 → 世界空间

// 光照参数
uniform vec3 sunShineAmbient;
uniform vec3 sunShineDiffuse;
uniform vec3 sunShineDir;
uniform vec3 sunShinePos;
uniform float sunShineIntensity; // 0=夜晚, 1=白天，地平线附近平滑过渡

// 阴影：阶段 2 已把 PCSS 计算 + 时域累积拆到独立 pass，这里只采样累积后的可见度。
// shadowVisibility ∈ [0,1]：1=完全受光，0=完全阴影（已含软阴影 + 时域降噪）。
uniform sampler2D shadowVisibility;
uniform vec3 uViewPos;

struct GBufferData {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    int blockType;
    float emissive;
    float roughness;
    float metallic;
};

// 从深度纹理 + 屏幕 UV 重建世界空间位置。
vec3 worldPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clip;
    viewPos /= viewPos.w;                 // 视图空间位置
    return (invView * viewPos).xyz;       // 世界空间位置
}

GBufferData readGBuffer(vec2 texCoord) {
    GBufferData data;
    float depth = texture(gDepth, texCoord).r;
    vec4 normalData = texture(gNormal, texCoord);
    vec4 albedoData = texture(gAlbedo, texCoord);
    vec4 propData = texture(gProperties, texCoord);

    data.position = worldPosFromDepth(texCoord, depth);
    data.normal = normalize(normalData.xyz);
    data.albedo = albedoData.rgb;
    data.blockType = int(propData.r * 255.0);
    data.emissive = propData.g;
    data.roughness = propData.b;
    data.metallic = propData.a;
    return data;
}

void main() {
    GBufferData data = readGBuffer(vTexCoord);
    if (data.blockType == 0) discard;

    vec3 dirLightDir = normalize(-sunShineDir);
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    // 阳光可见度：来自时域累积的阴影 pass（1=受光，0=阴影）。
    // 夜晚 sunShineIntensity 整体缩放阳光贡献，可见度自身已在阴影 pass 处理夜晚。
    float visibility = texture(shadowVisibility, vTexCoord).r;
    vec3 dirLightResult = visibility * dirDiffuse * sunShineIntensity;

    float ao = texture(ssao, vTexCoord).r;
    vec3 ambient = sunShineAmbient * data.albedo * ao;
    vec3 result = ambient + dirLightResult;

    FragColor = vec4(result, 1.0);
}
