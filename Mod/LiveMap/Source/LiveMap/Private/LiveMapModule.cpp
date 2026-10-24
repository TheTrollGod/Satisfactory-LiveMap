#include "LiveMapModule.h"

DEFINE_LOG_CATEGORY(LogLiveMap);

void FLiveMapModule::StartupModule()
{
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap module loaded"));
}

void FLiveMapModule::ShutdownModule()
{
}

IMPLEMENT_GAME_MODULE(FLiveMapModule, LiveMap);
