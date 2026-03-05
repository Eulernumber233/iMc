#version 330 core
out vec4 FragColor;

uniform vec3 uOutlineColor;
uniform float uTime;
uniform sampler2D uDepthTexture;
uniform int uDepthTestEnabled;

in float vPulseFactor;
in float vDepth;
in vec2 vTexCoord;

void main() {
    // 深度遮挡测试（如果启用）
    if (uDepthTestEnabled == 1) {
        float sceneDepth = texture(uDepthTexture, vTexCoord).r;
        float fragmentDepth = gl_FragCoord.z;
        float bias = 0.00055;
        if (sceneDepth + bias< fragmentDepth ) {
            // 如果场景深度小于当前片段深度（即当前片段被遮挡），则丢弃
            discard;
        }
    }

    // 轮廓颜色
    vec3 color = uOutlineColor;

    // 呼吸微调使颜色变化
    float pulse = sin(uTime * 2.0) * 0.1 + 0.9;
    color = color * pulse;

    // 边缘稍微变暗使边框更自然
    float edgeDarken = 0.8 + vPulseFactor * 0.2;
    color = color * edgeDarken;

    FragColor = vec4(color, 1.0);
}