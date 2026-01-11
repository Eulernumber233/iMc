#version 330 core
out vec4 FragColor;

uniform vec3 uOutlineColor;
uniform float uTime;
in float vPulseFactor;

void main() {
    // 基础颜色
    vec3 color = uOutlineColor;
    
    // 添加微弱的脉动颜色变化
    float pulse = sin(uTime * 2.0) * 0.1 + 0.9;
    color = color * pulse;
    
    // 边缘可以稍微变暗，让边框更有立体感
    float edgeDarken = 0.8 + vPulseFactor * 0.2;
    color = color * edgeDarken;
    
    FragColor = vec4(color, 1.0);
}