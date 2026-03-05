#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uTime;
uniform float uPulseSpeed;
uniform float uPulseIntensity;
uniform int uEnablePulse;

out float vPulseFactor;
out float vDepth;
out vec2 vTexCoord;

void main() {
    // 呼吸效果
    float pulse = 1.0;
    if (uEnablePulse == 1) {
        // 使用正弦函数产生呼吸效果
        pulse = 1.0 + sin(uTime * uPulseSpeed) * uPulseIntensity;
    }

    // 应用呼吸效果
    vec3 scaledPos = aPos * pulse;
    vPulseFactor = pulse;

    // 计算最终位置
    vec4 worldPos = uModel * vec4(scaledPos, 1.0);
    vec4 clipPos = uProjection * uView * worldPos;
    gl_Position = clipPos;

    // 计算NDC深度 (clipPos.z / clipPos.w)
    vDepth = clipPos.z / clipPos.w;

    // 计算纹理坐标用于深度纹理采样
    // 将NDC坐标[-1,1]转换到[0,1]范围
    vec2 ndc = clipPos.xy / clipPos.w;
    vTexCoord = ndc * 0.5 + 0.5;
}