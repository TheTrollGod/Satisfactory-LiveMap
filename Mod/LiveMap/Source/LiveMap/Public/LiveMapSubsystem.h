#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"

// Engine-free core from Private/Core. Included (not forward declared) so the
// UHT-generated constructors see complete types for the unique_ptrs.
#include "LiveMapCore.h"
#include "LiveMapServer.h"

#include <memory>

#include "LiveMapSubsystem.generated.h"

/**
 * Collects world state on the game thread and hands it to the WebSocket server.
 * Register it in the mod's GameWorldModule blueprint so SML spawns it with the
 * world. It only runs where the world is authoritative (host or dedicated server).
 */
UCLASS()
class LIVEMAP_API ALiveMapSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ALiveMapSubsystem();

	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	int32 Port = 8086;

	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	int32 DynamicTickHz = 5;

	/** Rails are rescanned this often, and only resent when they changed. */
	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	float StaticRescanSeconds = 10.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TickDynamic();
	void RescanStatic();
	void CollectDynamic(livemap::Frame& Out) const;

	FTimerHandle DynamicTimer;
	FTimerHandle StaticTimer;
	uint32 LastRailsHash = 0;
	bool bRailsPublished = false;

	// Plain C++ objects, not UObjects: the server thread must never see a UObject.
	std::unique_ptr<livemap::FrameDiffer> Differ;
	std::unique_ptr<livemap::Server> Server;
};
