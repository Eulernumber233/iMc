#version 330 core
out vec2 FragColor; // �����rgͨ���洢m1(���)��m2(���ƽ��)
flat in int vBlockType;

// ��ԴͶӰ����
uniform float dir_near;
uniform float dir_far;

// ���Ի���ȣ���[0,1]���תΪ������ȣ�
float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // NDC[-1,1]
    return (2.0 * dir_near * dir_far) / (dir_far + dir_near - z * (dir_far - dir_near));
}

const int BLOCK_ERRER = 255; // ���󷽿�����
void main()
{
    if (vBlockType == BLOCK_ERRER) discard;
    // ��ȡ��ǰ���ص���ȣ���һ��[0,1]��
    float depth = gl_FragCoord.z;
    float linear_depth = LinearizeDepth(depth); // תΪ������ȣ������������
    
    // �洢��Ⱦ�ֵm1�����ƽ����ֵm2
    FragColor = vec2(linear_depth, linear_depth * linear_depth + 1e-6);
}