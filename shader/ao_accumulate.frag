#version 330 core
// AO 时域累积。与阴影累积同构（标量版 TAA），但 AO 是**纯几何量**：光源移动不影响它，
// 静态世界里同一世界点的 AO 恒定，只有相机移动引入新可见区域（disocclusion）。
// 故可用很强的历史权重（低 alpha）收敛到接近 ground truth，邻域 clamp 只处理 disocclusion。

in vec2 TexCoords;
out float FragColor;

uniform sampler2D aoCurr;    // 当前帧单帧 HBAO（noisy）
uniform sampler2D aoHist;    // 上一帧累积 AO
uniform sampler2D depthTex;  // 当前帧深度（反算 motion vector）

uniform mat4 invViewProj;    // 当前帧未抖动 (proj*view) 的逆
uniform mat4 prevViewProj;   // 上一帧未抖动 proj*view
uniform vec2 screenSize;
uniform int  frameIndex;     // 首帧 / 历史无效时纯用当前帧

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    float curr = texture(aoCurr, TexCoords).r;

    if (frameIndex == 0) { FragColor = curr; return; }

    float d = texture(depthTex, TexCoords).r;
    if (d >= 1.0) { FragColor = curr; return; }   // 天空

    vec3 worldPos = worldFromDepth(TexCoords, d);
    vec4 prevClip = prevViewProj * vec4(worldPos, 1.0);
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        FragColor = curr; return;   // 上一帧不可见，无历史
    }

    float hist = texture(aoHist, prevUV).r;

    // 邻域 clamp：用当前帧 3x3 邻域 min/max 框住历史，处理 disocclusion / 重投影错误。
    // AO 几何恒定，正常情况下历史与当前几乎一致，clamp 几乎不裁剪；只在新露出的几何
    // 边缘（历史无效）把历史拉回当前范围，避免拖影。
    vec2 texel = 1.0 / screenSize;
    float nmin = curr, nmax = curr;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            if (x == 0 && y == 0) continue;
            float n = texture(aoCurr, TexCoords + vec2(x, y) * texel).r;
            nmin = min(nmin, n);
            nmax = max(nmax, n);
        }
    }
    hist = clamp(hist, nmin, nmax);

    // 混合：AO 几何恒定，静止/慢速时用很低的当前帧权重多攒历史（更干净）；
    // 快速移动时 disocclusion 多，略提当前帧权重减少拖影。
    float motionPixels = length((prevUV - TexCoords) * screenSize);
    float alpha = mix(0.05, 0.3, clamp(motionPixels / 20.0, 0.0, 1.0));

    FragColor = mix(hist, curr, alpha);
}
