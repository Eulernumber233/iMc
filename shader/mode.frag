#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;

// ── 纹理 ──
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_specular1;
uniform sampler2D texture_normal1;

// ── 阳光参数（由 CPU 端按昼夜计算，与延迟光照 pass 相同）──
uniform vec3 sunShineDir;
uniform vec3 sunShineAmbient;
uniform vec3 sunShineDiffuse;
uniform float sunShineIntensity;

// ── CSM 级联阴影 ──
// CASCADE_COUNT 与 C++ 端 Data.h 一致
#define CASCADE_COUNT 4
uniform sampler2DArray shadowMap;
uniform mat4 cascadeLightMatrix[CASCADE_COUNT];
uniform float cascadeSplitView[CASCADE_COUNT];
uniform float cascadeWorldExtent[CASCADE_COUNT];
uniform float uRefWorldExtent;
uniform int cascadeCount;
uniform mat4 view;
uniform float uShadowMapSize;

// ── 蓝噪声抖动 ──
uniform sampler2D blueNoiseTex;
uniform int blueNoiseSize;
uniform int frameIndex;

// ── PCSS 可调参数 ──
uniform int uBlockerSamples;
uniform int uFilterSamples;
uniform float uLightSizeUV;

// 保留兼容
uniform vec3 viewPos;

// ── 法线贴图 ──
vec3 applyNormalMapSimple(vec3 texNormal) {
    vec3 tangentNormal = texNormal * 2.0 - 1.0;
    return normalize(TBN * tangentNormal);
}

// ═══════════════════════════════════════════════════════════════
// PCSS 软阴影（移植自 shadow_visibility.frag）
// ═══════════════════════════════════════════════════════════════

const float MIN_FILTER_TEXELS = 1.5;
const float MAX_FILTER_TEXELS = 12.0;
const float TAU               = 6.2831853;
const float GOLDEN_ANGLE      = 2.39996323;

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

float CalculateBias(float NdotL) {
    float slope = sqrt(clamp(1.0 - NdotL * NdotL, 0.0, 1.0)) / max(NdotL, 1e-3);
    return clamp(2e-4 * slope + 5e-5, 5e-5, 1e-3);
}

float blueNoise(vec2 fragCoord) {
    vec2 uv = (fragCoord + 0.5) / float(blueNoiseSize);
    return texture(blueNoiseTex, uv).r;
}

mat2 ditherRotation(vec2 fragCoord) {
    float angle = blueNoise(fragCoord) * TAU + float(frameIndex) * GOLDEN_ANGLE;
    float ca = cos(angle), sa = sin(angle);
    return mat2(ca, -sa, sa, ca);
}

float FindBlockerAvgDepth(vec2 uv, float currentDepth, float searchRadiusUV, mat2 rot, int cascade) {
    float sum = 0.0;
    int count = 0;
    for (int i = 0; i < 16; i++) {
        if (i >= uBlockerSamples) break;
        vec2 sUV = uv + rot * poissonDisk[i] * searchRadiusUV;
        float d = texture(shadowMap, vec3(sUV, float(cascade))).r;
        if (d < currentDepth - 2e-4) { sum += d; count++; }
    }
    if (count == 0) return -1.0;
    return sum / float(count);
}

float PCSS_PCF_Filter(vec2 uv, float filterRadiusUV, float currentDepth, mat2 rot, int cascade) {
    float sum = 0.0;
    for (int i = 0; i < 16; i++) {
        if (i >= uFilterSamples) break;
        vec2 sUV = uv + rot * poissonDisk[i] * filterRadiusUV;
        float d = texture(shadowMap, vec3(sUV, float(cascade))).r;
        sum += (currentDepth <= d) ? 1.0 : 0.0;
    }
    return sum / float(max(uFilterSamples, 1));
}

float ShadowVisibilityCascade(vec3 worldPos, vec3 normal, vec3 lightDir, vec2 screenUV, int cascade) {
    vec4 fragPosLightSpace = cascadeLightMatrix[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0) return 0.0;

    float bias = CalculateBias(NdotL);
    float currentDepth = projCoords.z - bias;

    float texelUV = 1.0 / uShadowMapSize;
    mat2 rot = ditherRotation(screenUV);

    float lightSizeUV = uLightSizeUV * (uRefWorldExtent / max(cascadeWorldExtent[cascade], 1e-3));

    float blocker = FindBlockerAvgDepth(projCoords.xy, currentDepth, lightSizeUV, rot, cascade);
    if (blocker < 0.0) return 1.0;

    float penumbraUV = (currentDepth - blocker) / max(blocker, 1e-4) * lightSizeUV;
    float extentScale = uRefWorldExtent / max(cascadeWorldExtent[cascade], 1e-3);
    float filterRadiusUV = clamp(penumbraUV,
                                 MIN_FILTER_TEXELS * texelUV,
                                 MAX_FILTER_TEXELS * texelUV * extentScale);

    float visibility = PCSS_PCF_Filter(projCoords.xy, filterRadiusUV, currentDepth, rot, cascade);

    vec2 fade = min(projCoords.xy, 1.0 - projCoords.xy);
    float edgeFade = smoothstep(0.0, 0.05, min(fade.x, fade.y));
    float zFade = 1.0 - smoothstep(0.9, 1.0, projCoords.z);
    visibility = mix(1.0, visibility, edgeFade * zFade);

    return visibility;
}

float ShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir, vec2 screenUV) {
    float viewDepth = -(view * vec4(worldPos, 1.0)).z;

    int cascade = cascadeCount - 1;
    for (int i = 0; i < CASCADE_COUNT; i++) {
        if (i >= cascadeCount) break;
        if (viewDepth < cascadeSplitView[i]) { cascade = i; break; }
    }

    float visibility = ShadowVisibilityCascade(worldPos, normal, lightDir, screenUV, cascade);

    if (cascade + 1 < cascadeCount) {
        float splitFar = cascadeSplitView[cascade];
        float blendStart = splitFar * 0.9;
        float blend = smoothstep(blendStart, splitFar, viewDepth);
        if (blend > 0.0) {
            float visNext = ShadowVisibilityCascade(worldPos, normal, lightDir, screenUV, cascade + 1);
            visibility = mix(visibility, visNext, blend);
        }
    }

    return visibility;
}

// ═══════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════

void main()
{
    vec3 diffuseColor = texture(texture_diffuse1, TexCoords).rgb;

    // 法线贴图
    vec3 finalNormal;
    vec3 normalFromMap = texture(texture_normal1, TexCoords).rgb;
    if (length(normalFromMap) > 0.1) {
        finalNormal = applyNormalMapSimple(normalFromMap);
    } else {
        finalNormal = normalize(Normal);
    }

    vec3 dirLightDir = normalize(-sunShineDir);
    float NdotL = max(dot(finalNormal, dirLightDir), 0.0);

    // ── 环境光（已含昼夜权重）──
    vec3 ambient = sunShineAmbient * diffuseColor;

    // ── 阳光直射 ──
    vec3 dirDiffuse = sunShineDiffuse * NdotL * diffuseColor;

    // ── CSM PCSS 阴影可见度 ──
    float visibility = 1.0;
    if (sunShineIntensity > 0.001 && NdotL > 0.0) {
        visibility = ShadowVisibility(FragPos, finalNormal, dirLightDir, gl_FragCoord.xy);
    }
    vec3 dirLightResult = visibility * dirDiffuse;

    vec3 result = ambient + dirLightResult;
    FragColor = vec4(result, 1.0);
}
