#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "LiveMapTypes.h"
#include "LiveMapSubsystem.generated.h"

/**
 * Owns the collectors and the server. Register it in the mod's GameWorldModule
 * blueprint so SML spawns it with the world (server/host only).
 */
UCLASS()
class LIVEMAP_API ALiveMapSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ALiveMapSubsystem();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	int32 Port = 8086;

	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	float DynamicTickHz = 5.f;

private:
	void TickDynamic();
	FLiveMapDynamicFrame CollectDynamic() const;
	static FString FrameToJson(const FLiveMapDynamicFrame& Frame);

	FTimerHandle DynamicTimer;
};
