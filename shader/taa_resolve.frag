#version 330 core
// TAA resolve：当前帧合成色 + 历史累积 → 时域抗锯齿 / 降噪。
// 原理见 docs/taa-phase0-1.md。体素世界几乎全是静态几何，只有相机在动，
// 故 motion vector 直接从深度 + 上一帧 viewProj 反算，无需逐物体速度纹理。

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D currColor;   // 当前帧合成色（forward 之后）
uniform sampler2D history;     // 上一帧累积结果
uniform sampler2D depthTex;    // 当前帧深度（G-Buffer 深度纹理，32F）

uniform mat4 invViewProj;      // 当前帧未抖动 (proj*view) 的逆 → 屏幕+深度反算世界坐标
uniform mat4 prevViewProj;     // 上一帧未抖动 proj*view → 世界坐标投到上一帧屏幕
uniform vec2 screenSize;
uniform int  frameIndex;       // 首帧（==0）或历史无效时纯用当前帧

// 从屏幕 UV + 深度反算世界坐标
vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

// Catmull-Rom 历史重采样（5-tap 优化版，Filmic / SIGGRAPH 经典做法）。
// 双线性重采样每帧都会轻微模糊，历史反复累积 → 越来越糊（TAA over-smoothing）。
// Catmull-Rom 是带负权重的三次曲线，重采样时抵消模糊、保住锐度。
// 把 4x4 的 16 抽样用双线性硬件采样合并成 5 次 textureLod，省采样。
vec3 sampleHistoryCatmullRom(sampler2D tex, vec2 uv, vec2 texSize) {
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;

    vec2 f = samplePos - texPos1;

    // Catmull-Rom 权重
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // 合并相邻两抽样为一次双线性采样
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;

    vec2 texPos0  = texPos1 - 1.0;
    vec2 texPos3  = texPos1 + 2.0;
    vec2 texPos12 = texPos1 + offset12;

    texPos0  /= texSize;
    texPos3  /= texSize;
    texPos12 /= texSize;

    vec3 result = vec3(0.0);
    result += textureLod(tex, vec2(texPos0.x,  texPos0.y),  0.0).rgb * w0.x  * w0.y;
    result += textureLod(tex, vec2(texPos12.x, texPos0.y),  0.0).rgb * w12.x * w0.y;
    result += textureLod(tex, vec2(texPos3.x,  texPos0.y),  0.0).rgb * w3.x  * w0.y;

    result += textureLod(tex, vec2(texPos0.x,  texPos12.y), 0.0).rgb * w0.x  * w12.y;
    result += textureLod(tex, vec2(texPos12.x, texPos12.y), 0.0).rgb * w12.x * w12.y;
    result += textureLod(tex, vec2(texPos3.x,  texPos12.y), 0.0).rgb * w3.x  * w12.y;

    result += textureLod(tex, vec2(texPos0.x,  texPos3.y),  0.0).rgb * w0.x  * w3.y;
    result += textureLod(tex, vec2(texPos12.x, texPos3.y),  0.0).rgb * w12.x * w3.y;
    result += textureLod(tex, vec2(texPos3.x,  texPos3.y),  0.0).rgb * w3.x  * w3.y;

    return result;
}

// RGB ↔ YCoCg：在 YCoCg 空间做邻域 clamp，鬼影抑制更准、对亮度噪声更稳
vec3 RGBToYCoCg(vec3 c) {
    return vec3(
        0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        0.5  * c.r            - 0.5  * c.b,
       -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 YCoCgToRGB(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

void main() {
    vec3 curr = texture(currColor, TexCoords).rgb;

    // 历史无效 / 首帧：直接输出当前帧
    if (frameIndex == 0) {
        FragColor = vec4(curr, 1.0);
        return;
    }

    float d = texture(depthTex, TexCoords).r;
    // 天空（深度=1）没有可靠的世界坐标重投影，直接用当前帧（天空本身低频，不需累积）
    if (d >= 1.0) {
        FragColor = vec4(curr, 1.0);
        return;
    }

    // 1. motion vector：当前像素 → 世界坐标 → 上一帧屏幕 UV
    vec3 worldPos = worldFromDepth(TexCoords, d);
    vec4 prevClip = prevViewProj * vec4(worldPos, 1.0);
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // 上一帧不可见（越界）：无历史可用
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        FragColor = vec4(curr, 1.0);
        return;
    }

    // 用 Catmull-Rom 重采样历史（抵消累积模糊，保锐度），而非朴素双线性。
    // Catmull-Rom 的负权重可能产生轻微负色（亮边振铃），clamp 到非负防止反馈进历史。
    vec3 hist = max(sampleHistoryCatmullRom(history, prevUV, screenSize), vec3(0.0));

    // 2. neighborhood clamp（在 YCoCg 空间）：用当前帧 3x3 邻域的 AABB 框住历史色，
    //    抑制因遮挡变化 / 重投影错误产生的拖影（ghosting）。
    vec2 texel = 1.0 / screenSize;
    vec3 cYCoCg = RGBToYCoCg(curr);
    vec3 nmin = cYCoCg, nmax = cYCoCg;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            if (x == 0 && y == 0) continue;
            vec3 n = RGBToYCoCg(texture(currColor, TexCoords + vec2(x, y) * texel).rgb);
            nmin = min(nmin, n);
            nmax = max(nmax, n);
        }
    }
    vec3 hYCoCg = clamp(RGBToYCoCg(hist), nmin, nmax);
    hist = YCoCgToRGB(hYCoCg);

    // 3. 可变混合权重（当前帧权重 alpha）：
    //    - 静止/慢速：alpha 小（多攒历史 → 更干净、抗锯齿更强、噪声更低）
    //    - 快速移动/急转：alpha 大（历史不可靠 → 多信当前帧，减少拖影 ghosting）
    //    依据 motion vector 的像素移动量在 [0.05, 0.4] 间插值。
    float motionPixels = length((prevUV - TexCoords) * screenSize);
    float alpha = mix(0.05, 0.4, clamp(motionPixels / 20.0, 0.0, 1.0));
    vec3 result = mix(hist, curr, alpha);

    FragColor = vec4(result, 1.0);
}
