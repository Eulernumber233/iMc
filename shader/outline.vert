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
    float pulse = 1.0;
    if (uEnablePulse == 1) {
        pulse = 1.0 + sin(uTime * uPulseSpeed) * uPulseIntensity;
    }

    vec3 scaledPos = aPos * pulse;
    vPulseFactor = pulse;

    gl_Position = uProjection * uView * uModel * vec4(scaledPos, 1.0);
}
