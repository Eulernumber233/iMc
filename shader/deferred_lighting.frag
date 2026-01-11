#version 430 core

in vec2 vTexCoord;
out vec4 FragColor;

// G-Buffer纹理
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gProperties;

// 光照参数
uniform vec3 uLightDirection;    // 平行光方向
uniform vec3 uLightColor;        // 平行光颜色
uniform float uLightIntensity;   // 平行光强度
uniform vec3 uAmbientColor;      // 环境光颜色
uniform vec3 uViewPos;           // 相机位置

// 从G-Buffer读取数据
struct GBufferData {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    int blockType;
    float emissive;
    float roughness;
    float metallic;
};

GBufferData readGBuffer(vec2 texCoord) {
    GBufferData data;
    
    vec4 posData = texture(gPosition, texCoord);
    vec4 normData = texture(gNormal, texCoord);
    vec4 albedoData = texture(gAlbedo, texCoord);
    vec4 propData = texture(gProperties, texCoord);
    
    data.position = posData.xyz;
    data.normal = normalize(normData.xyz);
    data.albedo = albedoData.rgb;
    
    // 解码属性
    data.blockType = int(propData.r * 255.0);
    data.emissive = propData.g;
    data.roughness = propData.b;
    data.metallic = propData.a;
    
    return data;
}

// 简单的光照计算（平行光 + 环境光）
vec3 calculateLighting(GBufferData data) {
    // 环境光照
    vec3 ambient = uAmbientColor * data.albedo;
    
    // 平行光漫反射
    vec3 lightDir = normalize(-uLightDirection);
    float diff = max(dot(data.normal, lightDir), 0.0);
    vec3 diffuse = uLightColor * diff * data.albedo * uLightIntensity;
    
    // 合并光照
    vec3 lighting = ambient + diffuse;
    
    // 添加自发光
    lighting += data.albedo * data.emissive;
    
    return lighting;
}

void main() {
    // 从G-Buffer读取数据
    GBufferData data = readGBuffer(vTexCoord);
    
    // 如果是空气方块（或深度测试失败的像素），跳过
    if (data.blockType == 0) {
        discard;
    }
    
    // 计算光照
    //vec3 lighting = calculateLighting(data);
    
    vec3 lighting = data.albedo; // 环境光
    
    // 输出最终颜色
    FragColor = vec4(lighting, 1.0);
    //FragColor = vec4(0.1,0.5,0.3, 1.0);
}