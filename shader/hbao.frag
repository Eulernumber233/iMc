#version 330 core
// HBAO（Horizon-Based Ambient Occlusion，NVIDIA 2008）。
// 原理：对屏幕上每个像素，在其切平面上向 N 个方向"看"，沿每个方向步进采样深度，
// 求该方向能达到的最大仰角（horizon angle）；地平线越高 → 被遮挡越多。对所有方向
// 的水平角积分（sin(horizon) - sin(tangent)）得到遮蔽量。
//
// 配合 TAA：单帧故意少方向 + 少步数（噪声大但便宜），用蓝噪声抖动方向角 + 帧序号
// 时域去相关，由 ao_accumulate 跨帧累积成干净结果。AO 是纯几何量（光源移动不影响），
// 静态世界里同一世界点的 AO 恒定 → 时域累积可用很强的历史权重，收敛到接近 ground truth。

out float FragColor;
in vec2 TexCoords;

uniform sampler2D gDepth;     // 深度纹理（GL_DEPTH_COMPONENT32F）
uniform sampler2D gNormal;    // 世界空间法线
uniform sampler2D texNoise;   // 兼容保留（HBAO 改用蓝噪声，不再用 4x4 noise）

uniform vec2 screenSize;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 invProjection;   // 从深度重建视图空间位置

// 蓝噪声 + 帧序号时域去相关
uniform sampler2D blueNoiseTex;
uniform int blueNoiseSize;
uniform int frameIndex;

// HBAO 可调参数（来自 RuntimeConfig）
uniform int   uDirections;    // 采样方向数
uniform int   uSteps;         // 每个方向的步进次数
uniform float uRadius;        // 采样半径（世界尺度，米）
uniform float uIntensity;     // 遮蔽强度（指数）
uniform float uBias;          // 切线角 bias（弧度），抑制自遮挡导致的暗带

const float PI  = 3.14159265;
const float TAU = 6.2831853;

// 从深度纹理 + 屏幕 UV 重建视图空间位置（z 为负）。
vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clip;
    return viewPos.xyz / viewPos.w;
}

float blueNoise(vec2 fragCoord) {
    vec2 uv = (fragCoord + 0.5) / float(blueNoiseSize);
    return texture(blueNoiseTex, uv).r;
}

void main() {
    float depth = texture(gDepth, TexCoords).r;
    if (depth >= 1.0) { FragColor = 1.0; return; }   // 天空：无遮挡

    vec3 P = viewPosFromDepth(TexCoords, depth);                 // 视图空间位置
    vec3 N = normalize(mat3(view) * texture(gNormal, TexCoords).rgb); // 视图空间法线

    // 采样半径随距离透视缩放：把世界半径 uRadius 换算成屏幕 UV 半径。
    // 视图空间 z 越远，同样世界尺度对应的屏幕跨度越小。
    float radiusUV = uRadius * abs(projection[0][0]) / max(-P.z, 1e-3) * 0.5;
    // 半径过小（远处）时退化为无遮挡，避免步长小于一个纹素的退化采样
    if (radiusUV < 1.0 / screenSize.x) { FragColor = 1.0; return; }

    // 蓝噪声：方向起始角抖动（空间去相关）+ 帧序号黄金角旋转（时域去相关）
    float noise = blueNoise(gl_FragCoord.xy);
    float baseAngle = noise * TAU + float(frameIndex) * 2.39996323;
    // 步进起点抖动（打破环形条纹），每方向再叠加 noise
    float stepJitter = fract(noise + float(frameIndex) * 0.61803399);

    int   DIRS  = max(uDirections, 1);
    int   STEPS = max(uSteps, 1);
    float angleStep = TAU / float(DIRS);

    float occlusion = 0.0;

    for (int d = 0; d < DIRS; ++d) {
        float angle = baseAngle + float(d) * angleStep;
        vec2 dir = vec2(cos(angle), sin(angle));

        // 该方向的最大仰角（horizon）的 sin 值，初值为切线角（含 bias）
        // 切线角：法线在该屏幕方向上的倾斜决定了"地面"基准仰角
        float sinTangent = sin(-uBias);   // 简化基准；自遮挡由 bias 抑制
        float maxSinHorizon = sinTangent;

        for (int s = 1; s <= STEPS; ++s) {
            // 步进距离：抖动 + 均匀分布在 [0, radiusUV]
            float t = (float(s) - 1.0 + stepJitter) / float(STEPS);
            vec2 sampleUV = TexCoords + dir * t * radiusUV;
            if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
                break;

            float sd = texture(gDepth, sampleUV).r;
            if (sd >= 1.0) continue;                 // 天空，不构成遮挡

            vec3 S = viewPosFromDepth(sampleUV, sd); // 采样点视图空间位置
            vec3 H = S - P;                          // 指向采样点的向量
            float dist = length(H);
            if (dist < 1e-4) continue;

            // 该采样点相对切平面的仰角的 sin = (H·N)/|H|
            float sinHorizon = dot(H, N) / dist;

            // 距离衰减：超出世界半径 uRadius 的样本不算遮挡（平滑衰减）
            float falloff = clamp(1.0 - (dist * dist) / (uRadius * uRadius), 0.0, 1.0);

            // 只在抬高地平线时更新（horizon = 各样本仰角的最大值），含距离衰减
            if (sinHorizon > maxSinHorizon) {
                maxSinHorizon = mix(maxSinHorizon, sinHorizon, falloff);
            }
        }

        // 该方向遮蔽量 = sin(horizon) - sin(tangent)，clamp 到 [0,1]
        occlusion += clamp(maxSinHorizon - sinTangent, 0.0, 1.0);
    }

    occlusion /= float(DIRS);
    // 可见度 = 1 - 遮蔽，强度用指数控制
    float ao = pow(clamp(1.0 - occlusion, 0.0, 1.0), uIntensity);
    FragColor = ao;
}
