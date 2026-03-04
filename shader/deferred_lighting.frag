#version 430 core


in vec2 vTexCoord;
out vec4 FragColor;

// G-Buffer����
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gProperties;

uniform sampler2D ssao;

// ���ղ���
uniform vec3 sunShineAmbient;
uniform vec3 sunShineDiffuse;
uniform vec3 sunShineDir;       // ƽ�йⷽ��
uniform vec3 sunShinePos;

uniform sampler2D varianceShadowMap; // ȫ��ƽ�й� VSSM ���������ͼ��RG32F��
uniform float sunShineNear ;
uniform float sunShineFar;
uniform int SHADOW_WIDTH ;
uniform vec3 uViewPos;           // ���λ��
uniform mat4 lightSpaceMatrix; 

// ��G-Buffer��ȡ����
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
    vec4 normalData = texture(gNormal, texCoord);
    vec4 albedoData = texture(gAlbedo, texCoord);
    vec4 propData = texture(gProperties, texCoord);
    
    data.position = posData.xyz;
    data.normal = normalize(normalData.xyz);
    data.albedo = albedoData.rgb;
    
    // ��������
    data.blockType = int(propData.r * 255.0);
    data.emissive = propData.g;
    data.roughness = propData.b;
    data.metallic = propData.a;
    
    return data;
}

const float BIAS_COEFF = 0.0015;    // 阴影偏置系数
const float MIN_FILTER_SIZE = 1.8;   // 最小过滤核
const float MAX_FILTER_SIZE = 10.0;  // 最大过滤核
const float LIGHT_SIZE = 0.03;       // 光源大小（单位：世界空间）
const float DEPTH_THRESHOLD = 0.05;  // 深度阈值

// PoissonԲ�̲�����16��������
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);


float LinearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * sunShineNear * sunShineFar) / (sunShineFar + sunShineNear - z * (sunShineFar - sunShineNear));
}

// �Ľ���bias���㣬��ֹ����Ӱ
float CalculateBias(vec3 normal, vec3 lightDir, float NdotL) {
    float bias = BIAS_COEFF * tan(acos(clamp(NdotL, 0.0, 1.0)));
    bias = clamp(bias, BIAS_COEFF * 0.01, BIAS_COEFF * 0.2);
    return bias;
}

// VSSM���ļ���
float ChebyshevUpperBound(vec2 moments, float currentDepth) {
    // �ӷ�����ͼ����ȡ��ֵ��ƽ����ֵ
    float mean = moments.x;
    float mean2 = moments.y;
    
    // ���㷽��
    float variance = mean2 - (mean * mean);
    variance = max(variance, 0.0);
    
    // �������
    if (variance < 0.000001) {
        return currentDepth <= mean ? 1.0 : 0.0;
    }
    
    // �б�ѩ�򲻵�ʽ����
    float d = currentDepth - mean;
    if (d <= 0.0) {
        return 1.0; // ��ǰ���С�ڵ���ƽ����ȣ���ȫ�ɼ�
    }
    
    float p = variance / (variance + d * d);
    
    // ��ǿ�Աȶȣ���ֹ����
    float lightBleedingReduction = 0.2; // ���ƹ������Ƴ̶�
    p = clamp((p - lightBleedingReduction) / (1.0 - lightBleedingReduction), 0.0, 1.0);
    
    return p;
}

// �ڵ�������
float FindBlockerDepth_VSSM(vec3 projCoords, float currentDepth) {
    // ����Mipmap���ټ��
    vec2 mipMoments = textureLod(varianceShadowMap, projCoords.xy, 0.0).rg;
    float mipMean = mipMoments.r;
    
    // �����ų������û���ڵ�����
    if (mipMean <= 0.001 || currentDepth <= mipMean + 0.03) {
        return 0.0;
    }
    
    // ������Ȳ��������������
    float depthDiff = currentDepth - mipMean;
    
    if (depthDiff < 0.1) {
        // ����С������û���ڵ����ڵ�����
        // ����������ȷ��
        int found = 0;
        float sum = 0.0;
        
        for (int i = 0; i < 8; i++) {
            vec2 sampleUV = projCoords.xy + poissonDisk[i] * (1.0/SHADOW_WIDTH);
            float depth = texture(varianceShadowMap, sampleUV).r;
            if (depth > 0.001 && currentDepth > depth + 0.01) {
                sum += depth;
                found++;
            }
        }
        
        return found > 0 ? sum / float(found) : 0.0;
    } else {
        // ����󣬺ܿ������ڵ�
        // ֱ��ʹ��Mipmap��ֵ��Ϊ����
        return mipMean;
    }
}

// ��Ӱ����
float CalculatePenumbraSize(float avgBlockerDepth, float currentDepth, vec3 projCoords) {
    // ���û���ڵ��������С���˺�
    if (avgBlockerDepth <= 0.001) {
        return MIN_FILTER_SIZE;
    }
    
    // ��������ռ��еİ�Ӱ��С
    // PCSS��ʽ: (��Դ��С * (��ǰ��� - �ڵ������)) / �ڵ������
    float worldPenumbra = (LIGHT_SIZE * (currentDepth - avgBlockerDepth)) / avgBlockerDepth;
    
    // ������ռ䵥λת��Ϊ�����ռ䵥λ
    // ����ͶӰ�£�������ȷ�Χӳ�䵽��������
    float worldToTex = float(SHADOW_WIDTH) / (sunShineFar - sunShineNear);
    float texPenumbra = worldPenumbra * worldToTex;
    
    // ������Ȳ̬����
    float depthRatio = (currentDepth - avgBlockerDepth) / currentDepth;
    texPenumbra *= 1.0 + depthRatio * 2.0;
    
    // ���ƹ��˺˴�С
    return clamp(texPenumbra, MIN_FILTER_SIZE, MAX_FILTER_SIZE);
}

// �Ľ���PCF����
float PCSS_VSSM_Filter(vec3 projCoords, float filterSize, float currentDepth, float bias) {
    // �����������ش�С��ģ���뾶
    vec2 texelSize = 1.0 / textureSize(varianceShadowMap, 0);
    float radius = filterSize;
    
    float shadow = 0.0;
    int sampleCount = 0;
    
    // ʹ����ת��PoissonԲ�̲���
    float randomAngle = fract(sin(dot(projCoords.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.283185;
    mat2 rotation = mat2(cos(randomAngle), -sin(randomAngle),
                         sin(randomAngle), cos(randomAngle));
    
    // ���ݹ��˺˴�С������������
    int samples = int(clamp(radius * 2.0, 4.0, 16.0));
    
    for (int i = 0; i < samples; i++) {
        // ��ת������
        vec2 offset = rotation * poissonDisk[i % 16] * radius * texelSize;
        vec2 sampleUV = projCoords.xy + offset;
        
        // �߽���
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }
        
        // ����������ͼ
        vec2 moments = texture(varianceShadowMap, sampleUV).rg;
        
        // ʹ���б�ѩ�����ɼ���
        shadow += ChebyshevUpperBound(moments, currentDepth - bias);
        sampleCount++;
    }
    
    // ������Ч����
    if (sampleCount == 0) {
        vec2 moments = texture(varianceShadowMap, projCoords.xy).rg;
        return ChebyshevUpperBound(moments, currentDepth - bias);
    }
    
    return shadow / float(sampleCount);
}

// PCSS_VSSM ��Ӱ���㺯��
float ShadowCalculation_PCSS_VSSM(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // 1. ͶӰ����ת��
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // 2. �߽���
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    // ����������Ⱥ�bias
    float currentDepth = LinearizeDepth(projCoords.z);
    float NdotL = max(dot(normal, lightDir), 0.0);
    float bias = CalculateBias(normal, lightDir, NdotL);
    
    // �����ڵ���
    float blockerDepth = FindBlockerDepth_VSSM(projCoords, currentDepth);
    
    // �����Ӱ��С
    float filterSize = CalculatePenumbraSize(blockerDepth, currentDepth, projCoords);
    
    // ִ�й���
    float visibility = PCSS_VSSM_Filter(projCoords, filterSize, currentDepth, bias);
    
    // ������Ӱ����
    // Ӧ�����˥����ʹԶ������Ӱ�����
    float depthFactor = clamp(currentDepth / sunShineFar, 0.0, 1.0);
    visibility = mix(visibility, 1.0, depthFactor * 0.3);
    
    // ���ݷ��ߺ͹ⷽ��΢��
    visibility = mix(visibility, 1.0, (1.0 - NdotL) * 0.1);
    
    return 1.0 - visibility;
}

void main() {
    // ��G-Buffer��ȡ����
    GBufferData data = readGBuffer(vTexCoord);
    
    // ����ǿ������飨����Ȳ���ʧ�ܵ����أ�������
    if (data.blockType == 0) {
        discard;
    }
    
    // ƽ�й���ռ���
    vec3 dirLightDir = normalize(-sunShineDir);
    // ������
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    // ƽ�й���Ӱ
    vec4 FragPosLightSpace = lightSpaceMatrix * vec4(data.position,1.0); // Ƭ���ڹ�ռ��е�λ��

    float dirShadow = ShadowCalculation_PCSS_VSSM(FragPosLightSpace, data.normal, sunShineDir);
    vec3 dirLightResult = (1.0 - dirShadow) * dirDiffuse;

    // ��ȡ�������ڱ����� 
    float ao = texture(ssao, vTexCoord).r;
    //ao = 1;
    vec3 ambient = sunShineAmbient * data.albedo * ao;
    vec3 result = ambient + dirLightResult;
    //result = vec3(ao);
    // ���������ɫ
    //FragColor = vec4(vec3(ao),1.0);
    //FragColor = vec4(lighting,1.0);
    //FragColor = (1.0 - dirShadow) * vec4(data.albedo,1.0);
    FragColor = vec4(result,1.0);
}