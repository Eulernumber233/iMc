#version 430 core
// 阴影可见度 pass（阶段 2 时域累积的"当前帧"输入）。
// 单帧故意少样本 + 蓝噪声抖动（噪声大但便宜），输出每像素阳光可见度 [0,1]：
//   1.0 = 完全受光，0.0 = 完全阴影。
// 由 shadow_accumulate.frag 跨帧时域累积成干净结果，再喂给 deferred_lighting。
// 体素世界硬边 + 光源旋转会让阴影边缘逐 shadow-texel 翻转，PCSS 软化后表现为
// "一排格子波动"——时域累积把这种逐格翻转平滑成连续过渡。

in vec2 vTexCoord;
out float FragColor;   // 阳光可见度 [0,1]

// 阶段 3：CSM 级联阴影。单张 shadow map → 深度纹理数组（每级联一个 layer）。
// 按片元视距选级联，在选中 layer 里跑与阶段 2 完全相同的 PCSS；级联边界做淡入混合。
#define CASCADE_COUNT 4   // 须与 C++ 端 Data.h 的 CASCADE_COUNT 一致（数组 uniform 编译期定长）

// G-Buffer：深度重建世界位置 + 法线
uniform sampler2D gDepth;
uniform sampler2D gNormal;
uniform mat4 invProjection;
uniform mat4 invView;

// CSM 阴影贴图数组（普通深度）+ 逐级联矩阵 + 切分远边界
uniform sampler2DArray shadowMap;
uniform mat4  cascadeLightMatrix[CASCADE_COUNT];
uniform float cascadeSplitView[CASCADE_COUNT];  // 每级联远边界（视图空间正距离）
uniform float cascadeWorldExtent[CASCADE_COUNT]; // 每级联世界跨度（正交框全宽）
uniform float uRefWorldExtent;                  // 参考世界跨度（最远级联），半影归一化基准
uniform int   cascadeCount;                     // 实际级联数（≤ CASCADE_COUNT）
uniform mat4  view;                             // 算片元视图空间深度以选级联
uniform vec3 sunShineDir;
uniform float sunShineIntensity;

// debug：把每级联染成不同颜色输出（验证切分/接缝），平时设 0
#define CSM_DEBUG_TINT 0

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

float FindBlockerAvgDepth(vec2 uv, float currentDepth, float searchRadiusUV, mat2 rot, int cascade) {
    float sum = 0.0;
    int count = 0;
    int N = min(uBlockerSamples, 16);
    for (int i = 0; i < N; i++) {
        vec2 sUV = uv + rot * poissonDisk[i] * searchRadiusUV;
        float d = texture(shadowMap, vec3(sUV, float(cascade))).r;
        if (d < currentDepth - 2e-4) { sum += d; count++; }
    }
    if (count == 0) return -1.0;
    return sum / float(count);
}

float PCSS_PCF_Filter(vec2 uv, float filterRadiusUV, float currentDepth, mat2 rot, int cascade) {
    float sum = 0.0;
    int N = min(uFilterSamples, 16);
    for (int i = 0; i < N; i++) {
        vec2 sUV = uv + rot * poissonDisk[i] * filterRadiusUV;
        float d = texture(shadowMap, vec3(sUV, float(cascade))).r;
        sum += (currentDepth <= d) ? 1.0 : 0.0;
    }
    return sum / float(N);
}

// 在指定级联里跑完整 PCSS，返回阳光可见度 [0,1]：1=受光，0=阴影。
// 与阶段 2 单张逻辑完全一致，只是采样多了 cascade layer 维度。
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
    if (NdotL <= 0.0) return 0.0;   // 背光面：完全阴影

    float bias = CalculateBias(NdotL);
    float currentDepth = projCoords.z - bias;

    ivec3 smSize = textureSize(shadowMap, 0);
    float texelUV = 1.0 / float(smSize.x);

    mat2 rot = ditherRotation(screenUV);

    // 半影世界尺度归一化：uLightSizeUV 是 UV 量，近级联覆盖世界范围小 → 同一 UV 对应的
    // 世界半影更窄（阴影偏锐）。按 (参考跨度 / 本级联跨度) 放大近级联的 UV 半影，使世界
    // 半影宽度跨级联一致——shadow_light_size 的语义回到"单张覆盖全程时的半影"。
    float lightSizeUV = uLightSizeUV * (uRefWorldExtent / max(cascadeWorldExtent[cascade], 1e-3));

    float blocker = FindBlockerAvgDepth(projCoords.xy, currentDepth, lightSizeUV, rot, cascade);
    if (blocker < 0.0) return 1.0;   // 无遮挡：完全受光

    float penumbraUV = (currentDepth - blocker) / max(blocker, 1e-4) * lightSizeUV;
    // 半影上限同样按级联跨度放大：否则固定的 MAX_FILTER_TEXELS 纹素上限会把近级联放大后的
    // 半影重新夹窄，归一化白做。放大后单帧采样更稀疏，靠蓝噪声 + 时域累积补（与阶段 2 同理）。
    float extentScale = uRefWorldExtent / max(cascadeWorldExtent[cascade], 1e-3);
    float filterRadiusUV = clamp(penumbraUV,
                                 MIN_FILTER_TEXELS * texelUV,
                                 MAX_FILTER_TEXELS * texelUV * extentScale);

    float visibility = PCSS_PCF_Filter(projCoords.xy, filterRadiusUV, currentDepth, rot, cascade);

    // 边界淡出：接近阴影贴图边缘平滑过渡到完全受光（仅对最远级联有意义——更近级联的
    // 框外会落到下一级联，由级联混合接管；但统一处理也无害）
    vec2 fade = min(projCoords.xy, 1.0 - projCoords.xy);
    float edgeFade = smoothstep(0.0, 0.05, min(fade.x, fade.y));
    float zFade = 1.0 - smoothstep(0.9, 1.0, projCoords.z);
    visibility = mix(1.0, visibility, edgeFade * zFade);

    return visibility;
}

// CSM 入口：按视距选级联 + 级联边界淡入混合。
float ShadowVisibility(vec3 worldPos, vec3 normal, vec3 lightDir, vec2 screenUV) {
    // 片元视图空间深度（正值，越远越大）
    float viewDepth = -(view * vec4(worldPos, 1.0)).z;

    // 选级联：第一个 viewDepth < 切分远边界的级联
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewDepth < cascadeSplitView[i]) { cascade = i; break; }
    }

    float visibility = ShadowVisibilityCascade(worldPos, normal, lightDir, screenUV, cascade);

    // 级联边界淡入：在本级联远边界最后 10% 区间内，与下一级联混合，消除接缝硬线。
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

void main() {
    float depth = texture(gDepth, vTexCoord).r;
    // 天空：完全受光（不参与阴影），写 1.0
    if (depth >= 1.0) { FragColor = 1.0; return; }
    // 夜晚不计算阴影
    if (sunShineIntensity <= 0.001) { FragColor = 1.0; return; }

    vec3 worldPos = worldPosFromDepth(vTexCoord, depth);
    vec3 normal = normalize(texture(gNormal, vTexCoord).xyz);
    vec3 dirLightDir = normalize(-sunShineDir);

#if CSM_DEBUG_TINT
    // debug：把可见度按级联编号偏置输出（配合外部把 R 通道映射成伪彩看环带），
    // 验证完把 CSM_DEBUG_TINT 设回 0。此处直接用可见度叠加级联序号的小阶梯，肉眼可辨切分。
    float viewDepth = -(view * vec4(worldPos, 1.0)).z;
    int dbgCascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewDepth < cascadeSplitView[i]) { dbgCascade = i; break; }
    }
    FragColor = float(dbgCascade) / float(max(cascadeCount - 1, 1));
    return;
#endif

    FragColor = ShadowVisibility(worldPos, normal, dirLightDir, gl_FragCoord.xy);
}
