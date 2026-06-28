#version 430 core
// 阴影时域累积 pass。把当前帧的单帧 PCSS 可见度（噪声大）与历史累积重投影后混合，
// 跨帧攒成干净、稳定的可见度。专治"光源旋转时阴影边缘逐 shadow-texel 翻转 → 一排
// 格子波动"——单帧那一格翻转，时域累积把翻转摊到多帧，表现为连续淡入淡出而非跳变。
//
// 与 TAA resolve 同一套 motion vector（体素世界静态几何，仅相机动 → 从深度 +
// 上一帧 viewProj 反算）。但累积的是标量可见度，不是颜色。
// 光源是真在动的，故历史会"过期"——靠邻域 clamp 让真实变化透过、只平滑逐格噪声。

in vec2 vTexCoord;
out float FragColor;

uniform sampler2D shadowVisCurr;   // 当前帧单帧可见度（noisy）
uniform sampler2D shadowVisHist;   // 上一帧累积可见度
uniform sampler2D depthTex;        // 当前帧深度（反算 motion vector）

uniform mat4 invViewProj;          // 当前帧未抖动 (proj*view) 的逆
uniform mat4 prevViewProj;         // 上一帧未抖动 proj*view
uniform vec2 screenSize;
uniform int  frameIndex;           // 首帧 / 历史无效时纯用当前帧

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    float curr = texture(shadowVisCurr, vTexCoord).r;

    if (frameIndex == 0) { FragColor = curr; return; }

    float d = texture(depthTex, vTexCoord).r;
    if (d >= 1.0) { FragColor = curr; return; }   // 天空，无历史

    // motion vector：当前像素 → 世界坐标 → 上一帧屏幕 UV
    vec3 worldPos = worldFromDepth(vTexCoord, d);
    vec4 prevClip = prevViewProj * vec4(worldPos, 1.0);
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        FragColor = curr; return;   // 上一帧不可见，无历史
    }

    float hist = texture(shadowVisHist, prevUV).r;

    // 邻域 clamp：用当前帧 3x3 邻域的 min/max 框住历史。
    // 光源真移动时阴影确实在变，clamp 让真实变化透过（历史被拉回当前范围），
    // 只把单帧逐格翻转的噪声平滑掉。
    vec2 texel = 1.0 / screenSize;
    float nmin = curr, nmax = curr;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            if (x == 0 && y == 0) continue;
            float n = texture(shadowVisCurr, vTexCoord + vec2(x, y) * texel).r;
            nmin = min(nmin, n);
            nmax = max(nmax, n);
        }
    }
    hist = clamp(hist, nmin, nmax);

    // 混合：当前帧权重。静止/慢速可压低多攒历史；快速移动时历史不可靠多信当前。
    // 依据 motion vector 像素移动量在 [0.1, 0.5] 间插值。
    float motionPixels = length((prevUV - vTexCoord) * screenSize);
    float alpha = mix(0.1, 0.5, clamp(motionPixels / 20.0, 0.0, 1.0));

    FragColor = mix(hist, curr, alpha);
}
