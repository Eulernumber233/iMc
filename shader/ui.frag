#version 330 core
in vec2 TexCoord;
in vec4 Color;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform int uHasTexture = 0;
uniform int uIsText = 0;
uniform vec4 uTextColor = vec4(0.0, 0.0, 0.0, 1.0);
uniform float uAlpha = 1.0;

uniform vec2 uSize = vec2(100.0, 50.0);
uniform float uRadius = 5.0;
uniform int uIsRounded = 0;

void main()
{
    vec4 finalColor = Color;
    
    if (uHasTexture == 1) {
        finalColor = texture(uTexture, TexCoord);
        if (uIsText == 1) {
            float alpha = finalColor.a;
            finalColor = uTextColor;
            finalColor.a *= alpha;
        } else {
            // 对于普通纹理，进行alpha测试以丢弃完全透明的片段
            if (finalColor.a < 0.01) {
                discard;
            }
        }
    }
    
    if (uIsRounded == 1) {
        vec2 position = gl_FragCoord.xy;
        vec2 halfSize = uSize * 0.5;
        vec2 center = uSize * 0.5;
        
        vec2 q = abs(position - center) - halfSize + uRadius;
        float distance = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - uRadius;
        
        if (distance > 0.0) {
            discard;
        }
    }
    
    finalColor.a *= uAlpha;
    FragColor = finalColor;
}