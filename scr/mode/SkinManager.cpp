#include "SkinManager.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>

SkinManager& SkinManager::instance() {
    static SkinManager inst;
    return inst;
}

void SkinManager::init(const std::string& wideDir) {
    if (m_initialized) return;

    std::string searchPath = wideDir + "\\*.png";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::string filename(fd.cFileName);
        size_t dot = filename.rfind('.');
        if (dot == std::string::npos) continue;
        std::string name = filename.substr(0, dot);
        std::string fullPath = wideDir + "\\" + filename;

        m_skinNames.push_back(name);
        m_nameToPath[name] = fullPath;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    m_initialized = true;
}

std::string SkinManager::getSkinPath(const std::string& name) const {
    auto it = m_nameToPath.find(name);
    if (it != m_nameToPath.end()) return it->second;
    // fallback to default skin
    it = m_nameToPath.find(m_defaultSkin);
    if (it != m_nameToPath.end()) return it->second;
    return "";
}

std::string SkinManager::getRandomSkin(const std::string& exclude) const {
    std::vector<std::string> pool;
    for (const auto& name : m_skinNames) {
        if (name != exclude) pool.push_back(name);
    }
    if (pool.empty()) return exclude;
    int idx = rand() % static_cast<int>(pool.size());
    return pool[idx];
}
