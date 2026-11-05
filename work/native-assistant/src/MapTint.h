#pragma once
#include "json.hpp"
#include <vector>
namespace aa::maptint {
struct Marker {float x,y,z;};
std::vector<Marker> Markers();
bool Install(void* imageBase);
void Stop();
void SetSelection(const nlohmann::json& model,const nlohmann::json& preferences);
nlohmann::json Status();
}
