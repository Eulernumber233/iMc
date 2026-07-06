#pragma once
#include "ItemDefinition.h"
#include "../mode/PlayerModel.h"   // FirstPersonHandConfig
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <iostream>

// ── 手持物摆放 / 手臂参数注册表（单例，独立 JSON，热调不重编）──────────
// 数据源 assert/held_display.json，与 item_registry.json 完全分离——后者由
// gen 脚本生成，把 TRS 放进去会被覆盖，故单独一份。
//
// 结构（原版「少量模板 + 按需覆盖」的落地）：
//   arm       —— 第一人称手臂本体（空手挥手）参数，灌进 PlayerModel::handConfig。
//   profiles  —— 命名 TRS 档案，可被多个物品共享。每档 first/third 各一套 {t,r,s}。
//   defaults  —— 按模型类型的默认档案名（物品没显式指定时用）。
//   items     —— 物品 id → 档案名 的显式覆盖（独特物品在此点名）。
//
// 解析优先级：items[id] > defaults[modelType] > 内置预设 defaultHeldDisplay(type)。
// 首次访问惰性加载（无需 GL）。
class HeldDisplayRegistry {
public:
    static HeldDisplayRegistry& instance() {
        static HeldDisplayRegistry inst;
        return inst;
    }

    // 解析某物品最终摆放。返回引用指向稳定存储（m_profiles / m_builtin），可安全长借。
    const HeldItemDisplay& resolve(const ItemDefinition& def) {
        ensure();
        const std::string* prof = nullptr;
        auto it = m_itemProfile.find(def.id);
        if (it != m_itemProfile.end()) {
            prof = &it->second;
        } else {
            const std::string& d = m_defaultProfile[(int)def.modelType];
            if (!d.empty()) prof = &d;
        }
        if (prof) {
            auto pit = m_profiles.find(*prof);
            if (pit != m_profiles.end()) return pit->second;
        }
        return m_builtin[(int)def.modelType];   // 兜底：内置预设
    }

    // 第一人称手臂本体参数（含挥手时长/振幅）。
    const FirstPersonHandConfig& armConfig() {
        ensure();
        return m_arm;
    }

private:
    HeldDisplayRegistry() = default;

    void ensure() {
        if (!m_loaded) { m_loaded = true; load(); }
    }

    // 内置预设（JSON 缺失/缺档时兜底），与旧硬编码一致。
    void fillBuiltin() {
        m_builtin[(int)ItemModelType::EXTRUDED_2D]  = defaultHeldDisplay(ItemModelType::EXTRUDED_2D);
        m_builtin[(int)ItemModelType::BLOCK_CUBE]   = defaultHeldDisplay(ItemModelType::BLOCK_CUBE);
        m_builtin[(int)ItemModelType::CUSTOM_MODEL] = defaultHeldDisplay(ItemModelType::CUSTOM_MODEL);
    }

    // 去掉 JSON 里的 // 与 /* */ 注释（与 ItemRegistry / RuntimeConfig 同策略）。
    static std::string stripComments(const std::string& src) {
        std::string out; out.reserve(src.size());
        bool inString = false, inLine = false, inBlock = false;
        for (size_t i = 0; i < src.size(); ++i) {
            char c = src[i];
            char nc = (i + 1 < src.size()) ? src[i + 1] : '\0';
            if (inLine)  { if (c == '\n') { inLine = false; out.push_back(c); } continue; }
            if (inBlock) { if (c == '*' && nc == '/') { inBlock = false; ++i; } continue; }
            if (inString) {
                out.push_back(c);
                if (c == '\\') { out.push_back(nc); ++i; }
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '/' && nc == '/') { inLine = true; ++i; continue; }
            if (c == '/' && nc == '*') { inBlock = true; ++i; continue; }
            if (c == '"') inString = true;
            out.push_back(c);
        }
        return out;
    }

    // 解析一套 {t:[x,y,z], r:[x,y,z], s:scale}。缺字段保留 out 原值（可做部分覆盖）。
    static void parseTransform(const Json::Value& v, HeldTransform& out) {
        if (!v.isObject()) return;
        const Json::Value& t = v["t"];
        if (t.isArray() && t.size() == 3)
            out.translation = glm::vec3((float)t[0u].asDouble(), (float)t[1u].asDouble(), (float)t[2u].asDouble());
        const Json::Value& r = v["r"];
        if (r.isArray() && r.size() == 3)
            out.rotationDeg = glm::vec3((float)r[0u].asDouble(), (float)r[1u].asDouble(), (float)r[2u].asDouble());
        if (v.isMember("s")) out.scale = (float)v["s"].asDouble();
    }

    void parseArm(const Json::Value& a) {
        if (!a.isObject()) return;
        const Json::Value& o = a["offset"];
        if (o.isArray() && o.size() == 3)
            m_arm.offset = glm::vec3((float)o[0u].asDouble(), (float)o[1u].asDouble(), (float)o[2u].asDouble());
        if (a.isMember("pitch")) m_arm.pitch = (float)a["pitch"].asDouble();
        if (a.isMember("yaw"))   m_arm.yaw   = (float)a["yaw"].asDouble();
        if (a.isMember("roll"))  m_arm.roll  = (float)a["roll"].asDouble();
        if (a.isMember("scale")) m_arm.scale = (float)a["scale"].asDouble();
        if (a.isMember("swing_duration"))  m_arm.swingDuration  = (float)a["swing_duration"].asDouble();
        if (a.isMember("swing_pitch_amp")) m_arm.swingPitchAmp  = (float)a["swing_pitch_amp"].asDouble();
        if (a.isMember("swing_roll_amp"))  m_arm.swingRollAmp   = (float)a["swing_roll_amp"].asDouble();
        if (a.isMember("swing_lift"))      m_arm.swingLift      = (float)a["swing_lift"].asDouble();
    }

    void load(const std::string& path = "assert/held_display.json") {
        fillBuiltin();

        std::ifstream ifs(path);
        if (!ifs.good()) {
            std::cout << "[HeldDisplay] " << path << " not found, using built-in presets" << std::endl;
            return;
        }
        std::stringstream ss; ss << ifs.rdbuf();
        std::string clean = stripComments(ss.str());

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(clean, root)) {
            std::cerr << "[HeldDisplay] parse error: " << reader.getFormatedErrorMessages() << std::endl;
            return;
        }

        // arm
        parseArm(root["arm"]);

        // profiles：每档以「对应内置预设」为基再覆盖（缺 first/third 时退回预设）。
        const Json::Value& profs = root["profiles"];
        if (profs.isObject()) {
            for (const auto& name : profs.getMemberNames()) {
                const Json::Value& p = profs[name];
                // 以 flat 预设为基（大多数档案是 2D/工具），block/custom 档在 JSON 里给全即可。
                HeldItemDisplay disp = defaultHeldDisplay(ItemModelType::EXTRUDED_2D);
                parseTransform(p["first"], disp.firstPerson);
                parseTransform(p["third"], disp.thirdPerson);
                m_profiles[name] = disp;
            }
        }

        // defaults：按模型类型
        const Json::Value& defs = root["defaults"];
        if (defs.isObject()) {
            if (defs.isMember("extruded_2d"))  m_defaultProfile[(int)ItemModelType::EXTRUDED_2D]  = defs["extruded_2d"].asString();
            if (defs.isMember("block_cube"))   m_defaultProfile[(int)ItemModelType::BLOCK_CUBE]   = defs["block_cube"].asString();
            if (defs.isMember("custom_model")) m_defaultProfile[(int)ItemModelType::CUSTOM_MODEL] = defs["custom_model"].asString();
        }

        // items：物品 id → 档案名
        const Json::Value& items = root["items"];
        if (items.isObject()) {
            for (const auto& id : items.getMemberNames())
                m_itemProfile[id] = items[id].asString();
        }

        std::cout << "[HeldDisplay] loaded " << m_profiles.size() << " profiles, "
                  << m_itemProfile.size() << " item overrides" << std::endl;
    }

    bool m_loaded = false;
    FirstPersonHandConfig m_arm;                               // 手臂本体（默认值来自结构体内定义）
    std::unordered_map<std::string, HeldItemDisplay> m_profiles;
    std::unordered_map<std::string, std::string>     m_itemProfile;   // id → profile
    std::string m_defaultProfile[3];                           // 按 ItemModelType 索引
    HeldItemDisplay m_builtin[3];                              // 内置兜底，按 ItemModelType 索引
};
