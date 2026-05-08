#include "RuntimeConfig.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iostream>

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
    if (!reader.parse(content, root)) {
        std::cerr << "[RuntimeConfig] parse error: "
                  << reader.getFormatedErrorMessages() << " (using defaults)\n";
        return;
    }

    if (root.isMember("render_radius")) renderRadius = root["render_radius"].asInt();
    if (root.isMember("max_inflight_requests")) maxInflightRequests = root["max_inflight_requests"].asInt();
    if (root.isMember("max_uploads_per_frame")) maxUploadsPerFrame = root["max_uploads_per_frame"].asInt();
    if (root.isMember("worker_threads")) workerThreads = root["worker_threads"].asInt();
    if (root.isMember("print_profile_every_second")) printProfileEverySecond = root["print_profile_every_second"].asBool();

    std::cout << "[RuntimeConfig] loaded from " << path
              << " | render_radius=" << renderRadius
              << " worker_threads=" << workerThreads
              << " profile=" << (printProfileEverySecond ? "on" : "off") << std::endl;
}
