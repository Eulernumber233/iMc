#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 4) in vec3 aTangent;   // ����
layout (location = 5) in vec3 aBitangent; // ������

out vec3 FragPos;     // Ƭ��������ռ��λ��
out vec3 Normal;      // Ƭ��������ռ�ķ���
out vec2 TexCoords;
out mat3 TBN;         // ���߿ռ����

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


    // �����ߺ͸����߱任������ռ�
    vec3 T = normalize(mat3(model) * aTangent);
    vec3 B = normalize(mat3(model) * aBitangent);
    vec3 N = normalize(Normal);
    
    // Gram-Schmidt��������ȷ��T��N����
    T = normalize(T - dot(T, N) * N);
    
    // ���¼���B��ȷ��������
    B = normalize(cross(N, T));
    
    // ����TBN���󣨴����߿ռ䵽����ռ䣩
    TBN = mat3(T, B, N);
}