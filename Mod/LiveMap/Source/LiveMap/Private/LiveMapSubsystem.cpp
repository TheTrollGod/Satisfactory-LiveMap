#include "LiveMapSubsystem.h"
#include "LiveMapModule.h"

#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

#include "Buildables/FGBuildableRailroadTrack.h"
#include "FGCharacterPlayer.h"
#include "FGDroneVehicle.h"
#include "FGRailroadVehicle.h"
#include "FGTrain.h"
#include "WheeledVehicles/FGWheeledVehicle.h"

namespace
{
	std::string ToUtf8(const FString& S)
	{
		const FTCHARToUTF8 Conv(*S);
		return std::string(Conv.Get(), Conv.Length());
	}

	void Fill(livemap::Entity& E, const AActor* Actor, const FString& Name, const TCHAR* Kind)
	{
		const FVector P = Actor->GetActorLocation();
		E.Id = ToUtf8(Actor->GetName());
		E.Name = ToUtf8(Name);
		E.Kind = ToUtf8(Kind);
		E.X = P.X;
		E.Y = P.Y;
		E.Z = P.Z;
		E.Yaw = Actor->GetActorRotation().Yaw;
	}
}

ALiveMapSubsystem::ALiveMapSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}

void ALiveMapSubsystem::BeginPlay()
{
	Super::BeginPlay();

	Differ = std::make_unique<livemap::FrameDiffer>();
	Server = std::make_unique<livemap::Server>();

	livemap::ServerConfig Config;
	Config.Port = Port;
	Config.TickHz = FMath::Clamp(DynamicTickHz, 1, 30);
	std::string Error;
	if (!Server->Start(Config, &Error))
	{
		UE_LOG(LogLiveMap, Error, TEXT("LiveMap: could not listen on port %d: %s"), Port, UTF8_TO_TCHAR(Error.c_str()));
		Server.reset();
		return;
	}

	RescanStatic();
	GetWorldTimerManager().SetTimer(DynamicTimer, this, &ALiveMapSubsystem::TickDynamic, 1.f / Config.TickHz, true);
	GetWorldTimerManager().SetTimer(StaticTimer, this, &ALiveMapSubsystem::RescanStatic, StaticRescanSeconds, true);
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: listening on port %d at %d Hz"), Port, Config.TickHz);
}

void ALiveMapSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DynamicTimer);
	GetWorldTimerManager().ClearTimer(StaticTimer);
	if (Server)
	{
		Server->Stop(); // joins the server threads
		Server.reset();
	}
	Differ.reset();
	Super::EndPlay(EndPlayReason);
}

void ALiveMapSubsystem::TickDynamic()
{
	if (!Server)
	{
		return;
	}
	livemap::Frame Frame;
	CollectDynamic(Frame);
	std::string Delta = Differ->Update(Frame);
	Server->Publish(Differ->Snapshot(), std::move(Delta), Differ->Seq());
}

void ALiveMapSubsystem::CollectDynamic(livemap::Frame& Out) const
{
	UWorld* World = GetWorld();

	for (TActorIterator<AFGCharacterPlayer> It(World); It; ++It)
	{
		// TODO: offline players' bodies stay in the world on dedicated servers.
		// IsPlayerOnline() is protected; decide whether to show them greyed out.
		Fill(Out[livemap::ELayer::Players].emplace_back(), *It, It->GetCachedPlayerName(), TEXT("player"));
	}

	for (TActorIterator<AFGTrain> It(World); It; ++It)
	{
		// AFGTrain is a bookkeeping actor with no meaningful location; use the
		// lead vehicle. Trains mid-construction can have none.
		const AFGRailroadVehicle* Lead = It->GetFirstVehicle();
		if (!Lead)
		{
			continue;
		}
		livemap::Entity& E = Out[livemap::ELayer::Trains].emplace_back();
		Fill(E, Lead, It->GetTrainName().ToString(), TEXT("train"));
		E.Id = ToUtf8(It->GetName()); // stable across a lead-vehicle change
	}

	for (TActorIterator<AFGWheeledVehicle> It(World); It; ++It)
	{
		Fill(Out[livemap::ELayer::Vehicles].emplace_back(), *It, It->GetClass()->GetName(), TEXT("wheeled"));
	}

	for (TActorIterator<AFGDroneVehicle> It(World); It; ++It)
	{
		Fill(Out[livemap::ELayer::Vehicles].emplace_back(), *It, It->GetClass()->GetName(), TEXT("drone"));
	}
}

void ALiveMapSubsystem::RescanStatic()
{
	if (!Server)
	{
		return;
	}

	// Cheap pass first: did the set of track actors change at all?
	TArray<AFGBuildableRailroadTrack*> Tracks;
	uint32 Hash = 0;
	for (TActorIterator<AFGBuildableRailroadTrack> It(GetWorld()); It; ++It)
	{
		Tracks.Add(*It);
		Hash = HashCombine(Hash, GetTypeHash(It->GetFName()));
	}
	if (bRailsPublished && Hash == LastRailsHash)
	{
		return;
	}

	std::vector<livemap::Rail> Rails;
	Rails.reserve(Tracks.Num());
	for (const AFGBuildableRailroadTrack* Track : Tracks)
	{
		const USplineComponent* Spline = Track->GetSplineComponent();
		if (!Spline)
		{
			continue;
		}
		livemap::Rail& R = Rails.emplace_back();
		R.Id = ToUtf8(Track->GetName());
		// Sample along the spline (about every 10 m) so curves stay curved.
		const float Length = Spline->GetSplineLength();
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length / 1000.f), 1, 32);
		for (int32 I = 0; I <= Samples; ++I)
		{
			const FVector P = Spline->GetLocationAtDistanceAlongSpline(Length * I / Samples, ESplineCoordinateSpace::World);
			R.Points.push_back({P.X, P.Y, P.Z});
		}
	}

	LastRailsHash = Hash;
	bRailsPublished = true;
	Differ->SetRails(Rails);
	Server->Publish(Differ->Snapshot(), std::string(), Differ->Seq());
	Server->RequestResync();
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: published %d track segments"), static_cast<int32>(Rails.size()));
}
