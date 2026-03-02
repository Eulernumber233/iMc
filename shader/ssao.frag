#version 330 core
out float FragColor;
in vec2 TexCoords;

uniform sampler2D gPositionDepth;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];

// parameters (you'd probably want to use them as uniforms to more easily tweak the effect)
int kernelSize = 64;
float radius = 1.0;

uniform vec2 uScreenSize;
const float NOISE_SIZE = 4.0;  // 噪声纹理尺寸
vec2 noiseScale = uScreenSize / NOISE_SIZE;


uniform mat4 projection;
uniform mat4 view;
void main()
{

    vec3 fragPosWorld = texture(gPositionDepth, TexCoords).xyz;
    vec3 fragPos = (view * vec4(fragPosWorld, 1.0)).xyz;

        // 视线方向（视图空间）
    vec3 viewDir = normalize(-fragPos);

    vec3 normalWorld = texture(gNormal, TexCoords).rgb;
    vec3 normal = normalize(mat3(view) * normalWorld);

    vec3 randomVec = texture(texNoise, TexCoords * noiseScale).xyz;
   
    // Create TBN change-of-basis matrix: from tangent-space to view-space
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    // Iterate over the sample kernel and calculate occlusion factor
    
    float cosAngle = max(dot(normal, viewDir), 0.1);
    float adjustedRadius = radius;
    adjustedRadius *= cosAngle; // 视角与法线夹角大时，减小 radius
    adjustedRadius *= (1.0 - 0.002 * abs(fragPos.z)); // 远处物体 radius 更小
    
    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i) {
        vec3 samplePos = TBN * samples[i];
        samplePos = fragPos + samplePos * adjustedRadius;

        vec4 offset = projection * vec4(samplePos, 1.0);
        if (offset.w <= 0.0) continue;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        if (any(lessThan(offset.xy, vec2(0.0))) || any(greaterThan(offset.xy, vec2(1.0))))
            continue;

        vec3 sampleWorldPos = texture(gPositionDepth, offset.xy).xyz;
        float sampleDepth = (view * vec4(sampleWorldPos, 1.0)).z;
        // 原代码：
        // float rangeCheck = smoothstep(0.0, 1.0, adjustedRadius / abs(fragPos.z - sampleDepth));
        // occlusion += (sampleDepth > samplePos.z - 0.3 ? 1.0 : 0.0) * rangeCheck;

        // 修正后：
        float bias = 0.7; // 调整这个值，避免自身遮挡
        float depthDelta = abs(fragPos.z - sampleDepth);
        float rangeCheck = smoothstep(0.0, adjustedRadius * 2.0, depthDelta);
        rangeCheck = 1.0 - rangeCheck; // 深度差越小，rangeCheck 越接近1

        float occlusionFactor = smoothstep(samplePos.z - bias, samplePos.z, sampleDepth);
        occlusion += occlusionFactor * rangeCheck;
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    FragColor = occlusion;
    
    FragColor = occlusion;
}