// ===================== FILE: src/device_selector.cpp =====================
#include "device_selector.hpp"
#include "llm.hpp"
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

float getLocalMemoryGB() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long memTotalKB = 0;

    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal") != std::string::npos) {
            std::sscanf(line.c_str(), "MemTotal: %ld kB", &memTotalKB);
            break;
        }
    }
    return memTotalKB / 1024.0f / 1024.0f; // Convert to GB
}

float getRemoteMemoryGB(const std::string& ip) {
    std::string command = "ssh -o ConnectTimeout=2 " + ip + " cat /proc/meminfo | grep MemTotal";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return 0.0f;

    char buffer[256];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);

    long memTotalKB = 0;
    if (sscanf(result.c_str(), "MemTotal: %ld kB", &memTotalKB) == 1) {
        return memTotalKB / 1024.0f / 1024.0f; // Convert to GB
    } else {
        return 0.0f;
    }
}

std::vector<DeviceInfo> discover_devices(AppCliArgs* args) {
    std::vector<DeviceInfo> devices;
    float localMem = getLocalMemoryGB();
    devices.push_back({"root", localMem, ""});

    for (int i = 0; i < args->nWorkers; i++) {
        std::string name = "node" + std::to_string(i + 1);
        float remoteMem = getRemoteMemoryGB(args->workerHosts[i]);
        devices.push_back({name, remoteMem, args->workerHosts[i]});
    }
    return devices;
}

std::vector<DeviceInfo> sort_devices_by_memory(const std::vector<DeviceInfo>& devices) {
    auto sorted = devices;
    std::sort(sorted.begin(), sorted.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        return a.memory > b.memory;
    });
    return sorted;
}

std::vector<DeviceInfo> sort_devices_by_priority_list(const std::vector<DeviceInfo>& devices, const std::vector<std::string>& priority) {
    std::unordered_map<std::string, DeviceInfo> map;
    for (const auto& d : devices) map[d.name] = d;
    std::vector<DeviceInfo> ordered;
    for (const auto& name : priority) {
        if (map.count(name)) ordered.push_back(map[name]);
    }
    return ordered;
}

std::vector<DeviceInfo> select_devices_incrementally(const std::vector<DeviceInfo>& devices, float required_memory) {
    std::vector<DeviceInfo> selected;
    float accumulated = 0.0f;
    for (const auto& d : devices) {
        selected.push_back(d);
        accumulated += d.memory;
        if (accumulated >= required_memory) break;
    }
    return selected;
}

float estimate_required_memory(const char* modelPath) {
    LlmHeader header = loadLlmHeader(modelPath, 0, F_Q40);
    float totalBytes = static_cast<float>(header.nParams) * 2.0f; // Q40 ~ 2 bytes/param
    return totalBytes / (1024.0f * 1024.0f * 1024.0f); // Convert to GB
}
