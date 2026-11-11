#pragma once
namespace aa::game {
#ifdef AA_GAME_ATS
inline constexpr bool IsATS=true;
inline constexpr char Id[]="ats",SdkId[]="ats",AppId[]="270880",Version[]="0.1.0-ats";
inline constexpr char PanelTitle[]="ATS · Помощник достижений###AA",HintTitle[]="Помощник достижений ATS",ProfilePrompt[]="Выберите профиль ATS",Footer[]="0.1.0 ATS · Ctrl+F8";
inline constexpr wchar_t IdWide[]=L"ats",RuntimeFolder[]=L"ATSAchievementAssistant";
#else
inline constexpr bool IsATS=false;
inline constexpr char Id[]="ets2",SdkId[]="eut2",AppId[]="227300",Version[]="0.6.17-ets162";
inline constexpr char PanelTitle[]="ETS2 · Помощник достижений###AA",HintTitle[]="Помощник достижений ETS2",ProfilePrompt[]="Выберите профиль ETS2",Footer[]="0.6.17 · Ctrl+F8";
inline constexpr wchar_t IdWide[]=L"ets2",RuntimeFolder[]=L"ETS2AchievementAssistant";
#endif
}
