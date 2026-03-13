#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;  // 接收TBN矩阵


// 光源属性
struct Light {
    vec3 position;       // 光源位置
    vec3 ambient;        // 环境光强度
    vec3 diffuse;        // 漫反射光强度
    vec3 specular;       // 高光强度
    
    // 衰减系数
    float constant;      // 常数衰减
    float linear;        // 线性衰减
    float quadratic;     // 二次衰减
};

uniform sampler2D texture_diffuse1;   // 漫反射颜色
uniform sampler2D texture_specular1;  // 高光颜色
uniform sampler2D texture_normal1;    // 法线贴图
uniform sampler2D texture_roughness1;  // 粗糙度贴图
uniform sampler2D texture_ao1;         // 环境光遮蔽贴图
uniform float shininess;     // 高光反光度
uniform Light light;
uniform vec3 viewPos;

// 显示法线贴图效果
vec3 applyNormalMapSimple(vec3 texNormal) {
    vec3 tangentNormal = texNormal * 2.0 - 1.0;  
    return normalize(TBN * tangentNormal);
}


void main()
{    


    vec3 diffuseColor = texture(texture_diffuse1, TexCoords).rgb;

    float distance = length(light.position - FragPos);
    float attenuation = 1.0 / (light.constant + 
                              light.linear * distance + 
                              light.quadratic * (distance * distance));
    
    // 环境光分量
    vec3 ambient = light.ambient * diffuseColor;

        // 使用法线贴图计算法线
    vec3 finalNormal;
    vec3  normalFromMap = texture(texture_normal1, TexCoords).rgb;
    if(length(normalFromMap) > 0.1) {
        // 使用法线贴图
        finalNormal = applyNormalMapSimple(normalFromMap);
    } else {
        // 使用顶点法线
        finalNormal = normalize(Normal);
    }
     //finalNormal = normalize(Normal);

    // 漫反射分量
    vec3 lightDir = normalize(light.position - FragPos);
    float diff = max(dot(finalNormal, lightDir), 0.0);
    vec3 diffuse = light.diffuse * diff * diffuseColor;

    // 高光分量（Blinn-Phong）
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(finalNormal, halfwayDir), 0.0),  shininess);
    vec3 specular = light.specular * spec 
        * texture(texture_specular1, TexCoords).rgb;


    vec3 result = vec3(0.0);
    result += ambient;
    result += diffuse;
    result += specular;
    result *= attenuation;

    FragColor = vec4(result, 1.0);

    // 仅显示漫反射贴图（调试用，已注释）
     FragColor = texture(texture_diffuse1, TexCoords);
    //FragColor = vec4(1.0f);
}