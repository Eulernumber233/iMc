#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// 皮肤管理器：扫描 wide/ 目录，管理皮肤名称→路径映射
class SkinManager {
public:
    static SkinManager& instance();

    // 扫描指定目录下所有 .png 文件
    void init(const std::string& wideDir);
    bool isInitialized() const { return m_initialized; }

    const std::vector<std::string>& getSkinNames() const { return m_skinNames; }
    std::string getSkinPath(const std::string& name) const;
    int getSkinCount() const { return static_cast<int>(m_skinNames.size()); }

    // 随机选取皮肤，排除 exclude；若只剩 exclude 则返回 exclude
    std::string getRandomSkin(const std::string& exclude = "") const;
    const std::string& getDefaultSkin() const { return m_defaultSkin; }

private:
    SkinManager() = default;
    std::vector<std::string> m_skinNames;
    std::unordered_map<std::string, std::string> m_nameToPath;
    std::string m_defaultSkin = "steve";
    bool m_initialized = false;
};
