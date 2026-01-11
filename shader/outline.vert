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

void main() {
    // 脉动效果
    float pulse = 1.0;
    if (uEnablePulse == 1) {
        // 使用正弦函数创建脉动效果
        pulse = 1.0 + sin(uTime * uPulseSpeed) * uPulseIntensity;
    }
    
    // 应用脉动缩放
    vec3 scaledPos = aPos * pulse;
    vPulseFactor = pulse;
    
    // 计算最终位置
    vec4 worldPos = uModel * vec4(scaledPos, 1.0);
    gl_Position = uProjection * uView * worldPos;
}