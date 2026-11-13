#pragma once
#include <cstdint>
#include <filesystem>
#include "json.hpp"
namespace aa::jobprobe {
struct Addresses {uintptr_t predicate,specialPredicate,city,destination,name,update,rebuild,setStatus;};
void Install(void* imageBase,const std::filesystem::path& session,const Addresses& addresses);
void Stop();
void Flush();
nlohmann::json Status();
}
