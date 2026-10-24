#pragma once

#include "CoreMinimal.h"

// Plain data only. These cross from the game thread to the server thread,
// so they must never hold UObject pointers.

struct FLiveMapEntity
{
	FString Id;
	FString Name;
	FString Kind;
	FVector Pos = FVector::ZeroVector;
	float Yaw = 0.f;
	float Speed = 0.f;
};

struct FLiveMapDynamicFrame
{
	TArray<FLiveMapEntity> Players;
	TArray<FLiveMapEntity> Trains;
	TArray<FLiveMapEntity> Vehicles;
};
