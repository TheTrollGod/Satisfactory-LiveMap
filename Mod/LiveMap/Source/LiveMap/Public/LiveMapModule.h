#pragma once

#include "Modules/ModuleManager.h"
#include "Engine/World.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLiveMap, Log, All);

class FLiveMapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle WorldInitHandle;
	static void OnWorldInitializedActors(const UWorld::FActorsInitializedParams& Params);
};
