#include "RuntimeConfig.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iostream>

// 去掉 JSON 中的 C/C++ 风格注释（// 和 /* */），注意不触碰字符串内部
static std::string stripJsonComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool inString = false;
    bool inLineComment = false;
    bool inBlockComment = false;

    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char nc = (i + 1 < src.size()) ? src[i + 1] : '\0';

        if (inLineComment) {
            if (c == '\n') { inLineComment = false; out.push_back(c); }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && nc == '/') { inBlockComment = false; ++i; }
            continue;
        }
        if (inString) {
            out.push_back(c);
            if (c == '\\') { out.push_back(nc); ++i; } // 跳过转义字符
            else if (c == '"') { inString = false; }
            continue;
        }

        // 非字符串、非注释内
        if (c == '/' && nc == '/') { inLineComment = true; ++i; continue; }
        if (c == '/' && nc == '*') { inBlockComment = true; ++i; continue; }
        if (c == '"') { inString = true; }
        out.push_back(c);
    }
    return out;
}

const RuntimeConfig& RuntimeConfig::get() {
    static RuntimeConfig cfg;
    static bool loaded = false;
    if (!loaded) {
        cfg.loadFrom("assert/runtime_config.json");
        loaded = true;
    }
    return cfg;
}

void RuntimeConfig::loadFrom(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.good()) {
        std::cout << "[RuntimeConfig] " << path << " not found, using defaults\n";
        return;
    }

    // jsoncpp 0.5.0 的老 API：Reader::parse(string, Value)
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    Json::Value root;
    Json::Reader reader;
    std::string clean = stripJsonComments(content);
    if (!reader.parse(clean, root)) {
        std::cerr << "[RuntimeConfig] parse error: "
                  << reader.getFormatedErrorMessages() << " (using defaults)\n";
        return;
    }

    if (root.isMember("render_radius")) renderRadius = root["render_radius"].asInt();
    if (root.isMember("max_inflight_requests")) maxInflightRequests = root["max_inflight_requests"].asInt();
    if (root.isMember("max_uploads_per_frame")) maxUploadsPerFrame = root["max_uploads_per_frame"].asInt();
    if (root.isMember("worker_threads")) workerThreads = root["worker_threads"].asInt();
    if (root.isMember("vertical_cull_ratio")) verticalCullRatio = (float)root["vertical_cull_ratio"].asDouble();
    if (root.isMember("print_profile_every_second")) printProfileEverySecond = root["print_profile_every_second"].asBool();
    if (root.isMember("auto_save_interval_sec")) autoSaveIntervalSec = root["auto_save_interval_sec"].asInt();

    std::cout << "[RuntimeConfig] loaded from " << path
              << " | render_radius=" << renderRadius
              << " worker_threads=" << workerThreads
              << " profile=" << (printProfileEverySecond ? "on" : "off") << std::endl;
}
