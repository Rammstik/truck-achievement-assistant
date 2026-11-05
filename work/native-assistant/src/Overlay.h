#pragma once
#include <windows.h>
bool StartAssistant();
void StopAssistant();
void AssistantPaused(bool paused);
void AssistantGameplay(const char* name,const char* payload=nullptr);
void AssistantConfiguration(const char* name,const char* payload);
