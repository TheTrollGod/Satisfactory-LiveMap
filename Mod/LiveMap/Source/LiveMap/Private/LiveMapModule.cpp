#include "LiveMapModule.h"

#include "EngineUtils.h"
#include "LiveMapSubsystem.h"

DEFINE_LOG_CATEGORY(LogLiveMap);

void FLiveMapModule::StartupModule()
{
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap module loaded"));
	WorldInitHandle = FWorldDelegates::OnWorldInitializedActors.AddStatic(&FLiveMapModule::OnWorldInitializedActors);
}

void FLiveMapModule::ShutdownModule()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitHandle);
}

// There is no GameWorldModule blueprint, so spawn the subsystem ourselves on
// every authoritative game world.
void FLiveMapModule::OnWorldInitializedActors(const UWorld::FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (TActorIterator<ALiveMapSubsystem>(World))
	{
		return;
	}
	World->SpawnActor<ALiveMapSubsystem>();
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: spawned subsystem in %s"), *World->GetMapName());
}

IMPLEMENT_GAME_MODULE(FLiveMapModule, LiveMap);
