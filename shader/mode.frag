#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;  // 接收TBN矩阵


// 光源属性（平行光，与场景太阳光一致）
struct Light {
    vec3 direction;      // 光源方向（从光源指向场景）
    vec3 ambient;        // 环境光强度
    vec3 diffuse;        // 漫反射光强度
    vec3 specular;       // 高光强度
};

uniform sampler2D texture_diffuse1;   // 漫反射颜色
uniform sampler2D texture_specular1;  // 高光颜色
uniform sampler2D texture_normal1;    // 法线贴图
uniform float shininess;     // 高光反光度
uniform Light light;
uniform vec3 viewPos;

// 法线贴图应用
vec3 applyNormalMapSimple(vec3 texNormal) {
    vec3 tangentNormal = texNormal * 2.0 - 1.0;
    return normalize(TBN * tangentNormal);
}


void main()
{
    vec3 diffuseColor = texture(texture_diffuse1, TexCoords).rgb;

    // 环境光分量
    vec3 ambient = light.ambient * diffuseColor;

    // 使用法线贴图计算法线
    vec3 finalNormal;
    vec3 normalFromMap = texture(texture_normal1, TexCoords).rgb;
    if(length(normalFromMap) > 0.1) {
        finalNormal = applyNormalMapSimple(normalFromMap);
    } else {
        finalNormal = normalize(Normal);
    }

    // 漫反射分量（平行光：光线方向固定，不随距离衰减）
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(finalNormal, lightDir), 0.0);
    vec3 diffuse = light.diffuse * diff * diffuseColor;

    // 高光分量（Blinn-Phong）
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(finalNormal, halfwayDir), 0.0), shininess);
    vec3 specular = light.specular * spec
        * texture(texture_specular1, TexCoords).rgb;

    vec3 result = ambient + diffuse + specular;
    
    FragColor = vec4(result, 1.0);
}
