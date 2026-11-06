#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <algorithm>
#include <cctype>
#include <atomic>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <regex>
#include <shlobj.h>
#include "GameConfig.h"

namespace aa::i18n {
inline std::atomic<bool> english{true};
inline void SetLanguage(const std::string& language) { english=language!="ru"; }
inline const char* Language() { return english.load()?"en":"ru"; }
inline const char* Text(const char* russian) {
    if(!english.load())return russian;
    static const std::unordered_map<std::string,std::string> words={
#include "LocalizationData.h"
    };
    const auto found=words.find(russian);return found==words.end()?russian:found->second.c_str();
}
inline std::string GameLanguage(const std::filesystem::path& home) {
    static const std::regex pattern(R"aa(\buset\s+g_lang(?:_init)?\s+"([^"\r\n]+)")aa",std::regex::icase);
    for(const auto* name:{L"game.log.txt",L"config.cfg"}) {
        std::ifstream input(home/name,std::ios::binary);if(!input)continue;
        input.seekg(0,std::ios::end);const auto end=input.tellg();if(end<=0)continue;
        const auto length=std::min<std::streamoff>(static_cast<std::streamoff>(end),512*1024);
        input.seekg(0);std::string content(static_cast<size_t>(length),'\0');input.read(content.data(),length);
        if(static_cast<std::streamoff>(end)>length) {
            const auto start=std::max<std::streamoff>(length,static_cast<std::streamoff>(end)-2*1024*1024);
            input.seekg(start);std::string tail(static_cast<size_t>(static_cast<std::streamoff>(end)-start),'\0');input.read(tail.data(),tail.size());content+='\n';content+=tail;
        }
        std::string locale;
        for(auto i=std::sregex_iterator(content.begin(),content.end(),pattern);i!=std::sregex_iterator();++i)locale=(*i)[1].str();
        if(!locale.empty()) {
            for(auto& ch:locale)ch=static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            return locale=="ru"||locale.rfind("ru_",0)==0||locale.rfind("ru-",0)==0?"ru":"en";
        }
    }
    return "en";
}
inline std::string DefaultGameLanguage() {
    PWSTR documents=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents)))return "en";
    const auto home=std::filesystem::path(documents)/(aa::game::IsATS?L"American Truck Simulator":L"Euro Truck Simulator 2");
    CoTaskMemFree(documents);return GameLanguage(home);
}
}
