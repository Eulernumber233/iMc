#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;   // 切线
layout (location = 4) in vec3 aBitangent; // 副切线

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = vec3(worldPos);
    Normal = mat3(transpose(inverse(model))) * aNormal;

    vec2 TexCoords_copy = aTexCoords;   
    TexCoords = TexCoords_copy;    

    gl_Position = projection * view * worldPos;


    vec3 T = normalize(mat3(model) * aTangent);
    vec3 B = normalize(mat3(model) * aBitangent);
    vec3 N = normalize(Normal);
    
    T = normalize(T - dot(T, N) * N);
    
    B = normalize(cross(N, T));
    
    TBN = mat3(T, B, N);
}