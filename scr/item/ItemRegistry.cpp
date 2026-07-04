#include "ItemRegistry.h"
#include "ItemFactory.h"
#include "../Item.h"
#include "../TextureMgr.h"
#include "../RuntimeConfig.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iostream>

// ── 字符串 → 枚举 的小工具 ───────────────────────────────────────
static ItemCategory parseCategory(const std::string& s) {
    if (s == "block")    return ItemCategory::BLOCK;
    if (s == "tool")     return ItemCategory::TOOL;
    if (s == "material") return ItemCategory::MATERIAL;
    if (s == "food")     return ItemCategory::FOOD;
    return ItemCategory::MISC;
}

static ItemModelType parseModelType(const std::string& s) {
    if (s == "block_cube")   return ItemModelType::BLOCK_CUBE;
    if (s == "custom_model") return ItemModelType::CUSTOM_MODEL;
    return ItemModelType::EXTRUDED_2D;
}

// 方块名（与 GetBlockName 一致的小写形式）→ BlockType
static BlockType parseBlockType(const std::string& s) {
    if (s == "stone")  return BLOCK_STONE;
    if (s == "dirt")   return BLOCK_DIRT;
    if (s == "grass")  return BLOCK_GRASS;
    if (s == "water")  return BLOCK_WATER;
    if (s == "sand")   return BLOCK_SAND;
    if (s == "wood")   return BLOCK_WOOD;
    if (s == "leaves") return BLOCK_LEAVES;
    return BLOCK_AIR;
}

// 去掉 JSON 里的 // 与 /* */ 注释（与 RuntimeConfig 同一策略，允许注释配置）
static std::string stripJsonComments(const std::string& src) {
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

ItemRegistry& ItemRegistry::instance() {
    static ItemRegistry inst;
    return inst;
}

void ItemRegistry::load(const std::string& jsonPath) {
    if (m_loaded) return;
    m_loaded = true;

    std::ifstream ifs(jsonPath);
    if (!ifs.good()) {
        std::cout << "[ItemRegistry] " << jsonPath << " not found, no items loaded" << std::endl;
        return;
    }
    std::stringstream ss; ss << ifs.rdbuf();
    std::string clean = stripJsonComments(ss.str());

    // jsoncpp 0.5.0 老 API
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(clean, root)) {
        std::cerr << "[ItemRegistry] parse error: "
                  << reader.getFormatedErrorMessages() << std::endl;
        return;
    }

    // 支持两种顶层：直接数组，或 { "items": [...] }
    const Json::Value& arr = root.isArray() ? root : root["items"];
    if (!arr.isArray()) {
        std::cerr << "[ItemRegistry] expected a JSON array of items" << std::endl;
        return;
    }

    const bool debug = RuntimeConfig::get().debugMode;
    auto tm = TextureMgr::GetInstance();

    for (const auto& e : arr) {
        ItemDefinition def;
        def.id          = e.get("id", "").asString();
        if (def.id.empty()) continue;
        def.displayName = e.get("display_name", def.id).asString();
        // 图标在 TextureMgr 里用带前缀的名字注册，避免与方块等已有纹理名（stone/dirt/…）冲突
        // —— loadTexture2D 按名去重，同名会静默复用旧纹理而不加载物品图标路径。
        def.iconName    = "itemicon_" + def.id;
        def.iconPath    = e.get("icon", "").asString();
        def.category    = parseCategory(e.get("category", "misc").asString());
        def.maxStack    = e.get("max_stack", 64).asInt();
        def.hasDurability = e.get("has_durability", false).asBool();
        def.maxDurability = e.get("max_durability", 0).asInt();
        def.modelType   = parseModelType(e.get("model_type", "extruded_2d").asString());
        def.modelPath   = e.get("model_path", "").asString();
        def.blockType   = parseBlockType(e.get("block_type", "air").asString());
        def.behavior    = e.get("behavior", "generic").asString();
        def.loadInDebug = e.get("load_in_debug", false).asBool();

        // 绑定无状态行为对象（廉价，全部绑定）
        def.behaviorObj = ItemFactory::getBehavior(def.behavior);

        // 按 debug 过滤加载图标 GL 纹理。
        // TextureMgr basePath = "assert/textures/"，图标在工作目录相对路径 def.iconPath，
        // 用 "../../" 从 basePath 逃回工作目录再拼接。
        if ((!debug || def.loadInDebug) && !def.iconPath.empty()) {
            def.iconTexture = tm->LoadTexture2DManual(def.iconName, "../../" + def.iconPath, false);
            if (def.iconTexture != 0) ++m_loadedIcons;
        }

        m_defs.emplace(def.id, std::move(def));
    }

    std::cout << "[ItemRegistry] loaded " << m_defs.size() << " item defs, "
              << m_loadedIcons << " icons ("
              << (debug ? "debug" : "full") << " mode)" << std::endl;
    std::cerr << "[DIAG] ItemRegistry::load done defs=" << m_defs.size()
              << " icons=" << m_loadedIcons
              << " stone=" << (get("stone") ? "OK" : "NULL")
              << " dirt=" << (get("dirt") ? "OK" : "NULL") << std::endl;
}

const ItemDefinition* ItemRegistry::get(const std::string& id) const {
    auto it = m_defs.find(id);
    return it != m_defs.end() ? &it->second : nullptr;
}

const ItemDefinition* ItemRegistry::getByBlockType(BlockType type) const {
    if (type == BLOCK_AIR) return nullptr;
    for (const auto& kv : m_defs) {
        if (kv.second.category == ItemCategory::BLOCK && kv.second.blockType == type)
            return &kv.second;
    }
    return nullptr;
}

void ItemRegistry::forEach(const std::function<void(const ItemDefinition&)>& fn) const {
    for (const auto& kv : m_defs) fn(kv.second);
}

void ItemRegistry::forEachMutable(const std::function<void(ItemDefinition&)>& fn) {
    for (auto& kv : m_defs) fn(kv.second);
}
