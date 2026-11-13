#pragma once
#include "json.hpp"
#include <cstdint>
#include <vector>
namespace aa::maptint {
struct Marker {float x,y,z;};
std::vector<Marker> Markers();
bool Install(uintptr_t pack,uintptr_t packCall);
void Stop();
void SetSelection(const nlohmann::json& model,const nlohmann::json& preferences);
nlohmann::json Status();
}
