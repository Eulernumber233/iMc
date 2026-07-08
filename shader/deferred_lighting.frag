#version 430 core

in vec2 vTexCoord;
out vec4 FragColor;

// G-Buffer 纹理
uniform sampler2D gDepth;       // 深度纹理（GL_DEPTH_COMPONENT32F），用于重建位置
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gProperties;
uniform sampler2D aoTex;   // AO 通道（HBAO + 时域累积后的可见度）

// 从深度重建位置所需的逆矩阵
uniform mat4 invProjection;     // 深度 + 屏幕 UV → 视图空间
uniform mat4 invView;           // 视图空间 → 世界空间

// 光照参数（亮度/能量分配已在 CPU 端按昼夜算好折进下面两个颜色）：
uniform vec3 sunShineAmbient;
uniform vec3 sunShineDiffuse;
uniform vec3 sunShineDir;
uniform vec3 sunShinePos;
uniform float sunShineIntensity; // 保留兼容（=1.0）

// 阴影可见度
uniform sampler2D shadowVisibility;
uniform vec3 uViewPos;

// AO 随昼夜淡出
uniform float aoStrength;

// ── 光照缓存（体素洪水填充块光照）─────────────────────────────────
// binding=2: 所有 section 的光照数据拼成的扁平数组，每个 cell 为 packed RGBA8 uint
layout(std430, binding = 2) readonly buffer LightCacheSSBO {
    uint lightData[];        // 每 cell: R|G<<8|B<<16|A<<24
};

// binding=3: 相对 section 坐标 → lightData 偏移的查找表（-1 = 无数据）
layout(std430, binding = 3) readonly buffer SectionMapSSBO {
    int sectionOffsets[];    // 每 entry: lightData 中的基偏移，-1=无光
};

// 当前相机所在 section 的最小坐标（世界坐标/16 下取整），vec3 传入、shader 内转 ivec3
uniform vec3 camSecMin;
// section 坐标范围大小（各维度的 section 数）
uniform vec3 camSecRange;

// 从光缓存采样世界坐标位置的光照 RGB
vec3 sampleLightCache(vec3 worldPos) {
    // 转为整数 section 坐标
    ivec3 camMin  = ivec3(camSecMin);
    ivec3 camRange = ivec3(camSecRange);

    // 计算所在 section 坐标
    ivec3 sec;
    sec.x = int(floor(worldPos.x / 16.0));
    sec.y = int(floor(worldPos.y / 16.0));
    sec.z = int(floor(worldPos.z / 16.0));

    // 转为相对于相机 section 最小坐标的偏移
    ivec3 rel = sec - camMin;

    // 越界检查
    if (rel.x < 0 || rel.x >= camRange.x ||
        rel.y < 0 || rel.y >= camRange.y ||
        rel.z < 0 || rel.z >= camRange.z)
        return vec3(0.0);

    // 一维查找表索引
    int mapIdx = rel.x + rel.y * camRange.x + rel.z * camRange.x * camRange.y;

    // 边界安全检查
    if (mapIdx < 0 || mapIdx >= camRange.x * camRange.y * camRange.z)
        return vec3(0.0);

    int dataOffset = sectionOffsets[mapIdx];
    if (dataOffset < 0) return vec3(0.0);  // 该 section 无光照数据

    // Section 内局部坐标 (0..15)
    ivec3 local;
    local.x = int(mod(floor(worldPos.x), 16.0));
    local.y = int(mod(floor(worldPos.y), 16.0));
    local.z = int(mod(floor(worldPos.z), 16.0));
    // 处理负数
    local = (local + 16) % 16;

    int cellIdx = dataOffset + local.x + local.z * 16 + local.y * 256;

    // 边界检查（安全网）
    if (cellIdx < 0) return vec3(0.0);

    uint _packed = lightData[cellIdx];
    return vec3(float(_packed & 0xFFu) / 255.0,
                float((_packed >> 8) & 0xFFu) / 255.0,
                float((_packed >> 16) & 0xFFu) / 255.0);
}

struct GBufferData {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    int blockType;
    float emissive;
    float roughness;
    float metallic;
};

// 从深度纹理 + 屏幕 UV 重建世界空间位置。
vec3 worldPosFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clip;
    viewPos /= viewPos.w;                 // 视图空间位置
    return (invView * viewPos).xyz;       // 世界空间位置
}

GBufferData readGBuffer(vec2 texCoord) {
    GBufferData data;
    float depth = texture(gDepth, texCoord).r;
    vec4 normalData = texture(gNormal, texCoord);
    vec4 albedoData = texture(gAlbedo, texCoord);
    vec4 propData = texture(gProperties, texCoord);

    data.position = worldPosFromDepth(texCoord, depth);
    data.normal = normalize(normalData.xyz);
    data.albedo = albedoData.rgb;
    data.blockType = int(propData.r * 255.0);
    data.emissive = propData.g;
    data.roughness = propData.b;
    data.metallic = propData.a;
    return data;
}

void main() {
    GBufferData data = readGBuffer(vTexCoord);
    if (data.blockType == 0) discard;

    vec3 dirLightDir = normalize(-sunShineDir);
    float dirDiff = max(dot(data.normal, dirLightDir), 0.0);
    // 阳光直射项：sunShineDiffuse 已含 sunWeight，不再乘 sunShineIntensity
    vec3 dirDiffuse = sunShineDiffuse * dirDiff * data.albedo;

    // 阳光可见度：来自时域累积的阴影 pass（1=受光，0=阴影，已含软阴影）
    float visibility = texture(shadowVisibility, vTexCoord).r;
    vec3 dirLightResult = visibility * dirDiffuse;

    // 环境光底光：sunShineAmbient 已含 ambientWeight，乘 AO 做接触遮蔽
    float ao = texture(aoTex, vTexCoord).r;
    float effectiveAO = mix(1.0, ao, aoStrength);
    vec3 ambient = sunShineAmbient * data.albedo * effectiveAO;

    // ── 块光照（体素洪水填充）─────────────────────────────────────
    // 沿法线向"外侧"偏移采样，确保采样的是面朝外的空气格而不是方块自身格。
    // 偏移 0.01 足够跨过面边界（面在整数格边界上），同时又不影响 cell 归属。
    vec3 samplePos = data.position + data.normal * 0.01;
    vec3 blockLight = sampleLightCache(samplePos);
    // 块光照也受 AO 遮蔽（近方块内部光线被遮挡）
    vec3 blockLightResult = blockLight * data.albedo * effectiveAO;

    // 自发光（直接来源于 G-Buffer 的 emissive 分量）
    vec3 emissiveResult = data.albedo * data.emissive;

    vec3 result = ambient + dirLightResult + blockLightResult + emissiveResult;

    FragColor = vec4(result, 1.0);
}
