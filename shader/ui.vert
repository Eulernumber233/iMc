#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 uProjection;
uniform mat4 uTransform;
uniform vec4 uColor = vec4(1.0, 1.0, 1.0, 1.0);

void main()
{
    gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = uColor;
}