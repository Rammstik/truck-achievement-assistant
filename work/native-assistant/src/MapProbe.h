#pragma once
#include <filesystem>
#include "json.hpp"
namespace aa::mapprobe {
void Install(const std::filesystem::path& pluginDirectory,const std::filesystem::path& sessionDirectory);
void Stop();
void Arm();
void Flush();
nlohmann::json Status();
}
