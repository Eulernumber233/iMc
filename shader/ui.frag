#version 330 core
in vec2 TexCoord;
in vec4 Color;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform int uHasTexture = 0;
uniform int uIsText = 0;
uniform vec4 uTextColor = vec4(0.0, 0.0, 0.0, 1.0);
uniform float uAlpha = 1.0;

// 用于圆角矩形
uniform vec2 uSize = vec2(100.0, 50.0);
uniform float uRadius = 5.0;
uniform int uIsRounded = 0;

void main()
{
    vec4 finalColor = Color;
    
    if (uHasTexture == 1) {
        finalColor = texture(uTexture, TexCoord);
        if (uIsText == 1) {
            // 对于文本，使用纹理的alpha通道
            float alpha = finalColor.a;
            finalColor = uTextColor;
            finalColor.a *= alpha;
        }
    }
    
    // 圆角矩形处理
    if (uIsRounded == 1) {
        vec2 position = gl_FragCoord.xy;
        vec2 halfSize = uSize * 0.5;
        vec2 center = uSize * 0.5;
        
        // 计算到四个角的距离
        vec2 q = abs(position - center) - halfSize + uRadius;
        float distance = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - uRadius;
        
        if (distance > 0.0) {
            discard;
        }
    }
    
    // 应用整体透明度
    finalColor.a *= uAlpha;
    FragColor = finalColor;
}