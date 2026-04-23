#version 330 core
out vec4 FragColor;

uniform vec3 uOutlineColor;
uniform float uTime;

in float vPulseFactor;

void main() {
    // 遮挡由硬件深度测试（GL_LEQUAL + polygon offset）处理，
    // 这里不再手动比较场景深度纹理。

    vec3 color = uOutlineColor;

    float pulse = sin(uTime * 2.0) * 0.1 + 0.9;
    color = color * pulse;

    float edgeDarken = 0.8 + vPulseFactor * 0.2;
    color = color * edgeDarken;

    FragColor = vec4(color, 1.0);
}
