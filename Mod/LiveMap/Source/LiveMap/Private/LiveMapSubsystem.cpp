#include "LiveMapSubsystem.h"
#include "LiveMapModule.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "FGCharacterPlayer.h"
#include "FGTrain.h"
#include "WheeledVehicles/FGWheeledVehicle.h"

ALiveMapSubsystem::ALiveMapSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}

void ALiveMapSubsystem::BeginPlay()
{
	Super::BeginPlay();

	// TODO(M2): start the uWS server on its own thread here.
	const float Interval = 1.f / FMath::Max(DynamicTickHz, 0.1f);
	GetWorldTimerManager().SetTimer(DynamicTimer, this, &ALiveMapSubsystem::TickDynamic, Interval, true);

	UE_LOG(LogLiveMap, Log, TEXT("LiveMap subsystem started, port %d, %.1f Hz"), Port, DynamicTickHz);
}

void ALiveMapSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DynamicTimer);
	// TODO(M2): stop the server thread and join it.
	Super::EndPlay(EndPlayReason);
}

void ALiveMapSubsystem::TickDynamic()
{
	const FString Json = FrameToJson(CollectDynamic());
	// TODO(M2): diff against the previous frame and enqueue to the server thread.
	UE_LOG(LogLiveMap, Verbose, TEXT("%s"), *Json);
}

FLiveMapDynamicFrame ALiveMapSubsystem::CollectDynamic() const
{
	FLiveMapDynamicFrame Frame;
	UWorld* World = GetWorld();

	for (TActorIterator<AFGCharacterPlayer> It(World); It; ++It)
	{
		FLiveMapEntity& E = Frame.Players.AddDefaulted_GetRef();
		E.Id = It->GetName();
		// TODO: resolve the display name via the player state.
		E.Name = E.Id;
		E.Kind = TEXT("player");
		E.Pos = It->GetActorLocation();
		E.Yaw = It->GetActorRotation().Yaw;
	}

	for (TActorIterator<AFGTrain> It(World); It; ++It)
	{
		// TODO: verify against FGTrain.h; position should come from the lead
		// locomotive (GetFirstVehicle or similar), not the train actor itself.
		FLiveMapEntity& E = Frame.Trains.AddDefaulted_GetRef();
		E.Id = It->GetName();
		E.Name = It->GetTrainName().ToString();
		E.Kind = TEXT("train");
		E.Pos = It->GetActorLocation();
	}

	for (TActorIterator<AFGWheeledVehicle> It(World); It; ++It)
	{
		FLiveMapEntity& E = Frame.Vehicles.AddDefaulted_GetRef();
		E.Id = It->GetName();
		E.Kind = It->GetClass()->GetName();
		E.Pos = It->GetActorLocation();
		E.Yaw = It->GetActorRotation().Yaw;
	}

	return Frame;
}

FString ALiveMapSubsystem::FrameToJson(const FLiveMapDynamicFrame& Frame)
{
	auto ToArray = [](const TArray<FLiveMapEntity>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FLiveMapEntity& E : Items)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), E.Id);
			Obj->SetStringField(TEXT("name"), E.Name);
			Obj->SetStringField(TEXT("kind"), E.Kind);
			Obj->SetArrayField(TEXT("pos"), {
				MakeShared<FJsonValueNumber>(E.Pos.X),
				MakeShared<FJsonValueNumber>(E.Pos.Y),
				MakeShared<FJsonValueNumber>(E.Pos.Z) });
			Obj->SetNumberField(TEXT("yaw"), E.Yaw);
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	};

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("delta"));
	Root->SetArrayField(TEXT("players"), ToArray(Frame.Players));
	Root->SetArrayField(TEXT("trains"), ToArray(Frame.Trains));
	Root->SetArrayField(TEXT("vehicles"), ToArray(Frame.Vehicles));

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}
