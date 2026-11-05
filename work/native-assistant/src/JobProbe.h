#pragma once
#include <filesystem>
#include "json.hpp"
namespace aa::jobprobe {
void Install(void* imageBase,const std::filesystem::path& session);
void Stop();
void Flush();
nlohmann::json Status();
}
