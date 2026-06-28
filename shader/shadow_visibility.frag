#version 430 core
// 阴影可见度 pass（阶段 2 时域累积的"当前帧"输入）。
// 单帧故意少样本 + 蓝噪声抖动（噪声大但便宜），输出每像素阳光可见度 [0,1]：
//   1.0 = 完全受光，0.0 = 完全阴影。
// 由 shadow_accumulate.frag 跨帧时域累积成干净结果，再喂给 deferred_lighting。
// 体素世界硬边 + 光源旋转会让阴影边缘逐 shadow-texel 翻转，PCSS 软化后表现为
// "一排格子波动"——时域累积把这种逐格翻转平滑成连续过渡。

in vec2 vTexCoord;
out float FragColor;   // 阳光可见度 [0,1]

// G-Buffer：深度重建世界位置 + 法线
uniform sampler2D gDepth;
uniform sampler2D gNormal;
uniform mat4 invProjection;
uniform mat4 invView;

// 阴影贴图（普通深度）
uniform sampler2D shadowMap;
uniform mat4 lightSpaceMatrix;
uniform vec3 sunShineDir;
uniform float sunShineIntensity;

// 蓝噪声抖动 + 帧序号时域去相关
uniform sampler2D blueNoiseTex;
uniform int blueNoiseSize;
uniform int frameIndex;

// 可调参数（来自 RuntimeConfig）
uniform int   uBlockerSamples;
uniform int   uFilterSamples;
uniform float uLightSizeUV;

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

vec3 worldPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clip;
    viewPos /= viewPos.w;
    return (invView * viewPos).xyz;
}

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

float FindBlockerAvgDepth(vec2 uv, float currentDepth, float searchRadiusUV, mat2 rot) {
    float sum = 0.0;
    int count = 0;
    int N = min(uBlockerSamples, 16);
    for (int i = 0; i < N; i++) {
        vec2 sUV = uv + rot * poissonDisk[i] * searchRadiusUV;
        float d = texture(shadowMap, sUV).r;
        if (d < currentDepth - 2e-4) { sum += d; count++; }
    }
    if (count == 0) return -1.0;
    return sum / float(count);
}

float PCSS_PCF_Filter(vec2 uv, float filterRadiusUV, float currentDepth, mat2 rot) {
    float sum = 0.0;
    int N = min(uFilterSamples, 16);
    for (int i = 0; i < N; i++) {
        vec2 sUV = uv + rot * poissonDisk[i] * filterRadiusUV;
        float d = texture(shadowMap, sUV).r;
        sum += (currentDepth <= d) ? 1.0 : 0.0;
    }
    return sum / float(N);
}

// 返回阳光可见度 [0,1]：1=受光，0=阴影
float ShadowVisibility(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir, vec2 screenUV) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float NdotL = max(dot(normal, lightDir), 0.0);
    if (NdotL <= 0.0) return 0.0;   // 背光面：完全阴影

    float bias = CalculateBias(NdotL);
    float currentDepth = projCoords.z - bias;

    ivec2 smSize = textureSize(shadowMap, 0);
    float texelUV = 1.0 / float(smSize.x);

    mat2 rot = ditherRotation(screenUV);

    float blocker = FindBlockerAvgDepth(projCoords.xy, currentDepth, uLightSizeUV, rot);
    if (blocker < 0.0) return 1.0;   // 无遮挡：完全受光

    float penumbraUV = (currentDepth - blocker) / max(blocker, 1e-4) * uLightSizeUV;
    float filterRadiusUV = clamp(penumbraUV,
                                 MIN_FILTER_TEXELS * texelUV,
                                 MAX_FILTER_TEXELS * texelUV);

    float visibility = PCSS_PCF_Filter(projCoords.xy, filterRadiusUV, currentDepth, rot);

    // 边界淡出：接近阴影贴图边缘平滑过渡到完全受光
    vec2 fade = min(projCoords.xy, 1.0 - projCoords.xy);
    float edgeFade = smoothstep(0.0, 0.05, min(fade.x, fade.y));
    float zFade = 1.0 - smoothstep(0.9, 1.0, projCoords.z);
    visibility = mix(1.0, visibility, edgeFade * zFade);

    return visibility;
}

void main() {
    float depth = texture(gDepth, vTexCoord).r;
    // 天空：完全受光（不参与阴影），写 1.0
    if (depth >= 1.0) { FragColor = 1.0; return; }
    // 夜晚不计算阴影
    if (sunShineIntensity <= 0.001) { FragColor = 1.0; return; }

    vec3 worldPos = worldPosFromDepth(vTexCoord, depth);
    vec3 normal = normalize(texture(gNormal, vTexCoord).xyz);
    vec3 dirLightDir = normalize(-sunShineDir);

    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(worldPos, 1.0);
    FragColor = ShadowVisibility(fragPosLightSpace, normal, dirLightDir, gl_FragCoord.xy);
}
