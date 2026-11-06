#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "Localization.h"
int main(int argc,char** argv) {
    assert(argc==2);std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    using namespace aa::i18n;
    assert(GameLanguage(root)=="en");
    {std::ofstream(root/"config.cfg")<<"uset g_lang_init \"ru_ru\"\n";}
    assert(GameLanguage(root)=="ru");
    {std::ofstream(root/"game.log.txt")<<"uset g_lang_init \"ru_ru\"\nuset g_lang \"de_de\"\n";}
    assert(GameLanguage(root)=="en");
    {std::ofstream out(root/"game.log.txt");out<<"uset g_lang \"ru_ru\"\n"<<std::string(3000000,'x');}
    assert(GameLanguage(root)=="ru");
    {std::ofstream(root/"game.log.txt",std::ios::app)<<"\nuset g_lang \"fr_fr\"\n";}
    assert(GameLanguage(root)=="en");
    SetLanguage("en");assert(std::string(Text("Настройки"))=="Settings");
    SetLanguage("ru");assert(std::string(Text("Настройки"))=="Настройки");
    return 0;
}
