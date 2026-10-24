#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Subsystem/ModSubsystem.h"

// Engine-free core from Private/Core. Included (not forward declared) so the
// UHT-generated constructors see complete types for the smart pointers.
#include "LiveMapCore.h"
#include "LiveMapServer.h"

#include <memory>

#include "LiveMapSubsystem.generated.h"

/** A static layer being encoded on the thread pool; resolves to its served version. */
struct FLiveMapStaticJob
{
	std::string Name;
	TFuture<uint64> Version;
};

/**
 * Collects world state on the game thread and hands it to the WebSocket server.
 * FLiveMapModule spawns it on every authoritative world (host or dedicated server).
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

	/** Rails and buildings are rescanned this often, and only resent when they changed. */
	UPROPERTY(EditDefaultsOnly, Category = "LiveMap")
	float StaticRescanSeconds = 10.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TickDynamic();
	void RescanStatic();
	void RescanRails();
	void RescanBuildings();
	void CollectDynamic(livemap::Frame& Out) const;
	/** Picks up static documents the worker finished encoding. */
	void PollStaticJobs();
	const livemap::BuildingClass& DescribeClass(UClass* Class, const class AFGBuildable* Sample, const FBox* SampleBox);

	FTimerHandle DynamicTimer;
	FTimerHandle StaticTimer;
	uint32 LastRailsHash = 0;
	bool bRailsPublished = false;
	uint64 LastBuildingsHash = 0;
	uint64 LastLinesHash = 0;

	TArray<FLiveMapStaticJob> StaticJobs;

	/** Sampled belt/pipe/hypertube splines by actor name. They never change once built. */
	TMap<FName, livemap::Polyline> SplineCache;

	/** Footprint, name and category per buildable class. Classes live for the whole session. */
	TMap<const UClass*, livemap::BuildingClass> ClassCache;

	// Plain C++ objects, not UObjects: the server threads must never see a UObject.
	// Shared so an encode job on the thread pool can outlive a tick.
	std::unique_ptr<livemap::FrameDiffer> Differ;
	std::shared_ptr<livemap::Server> Server;
};
