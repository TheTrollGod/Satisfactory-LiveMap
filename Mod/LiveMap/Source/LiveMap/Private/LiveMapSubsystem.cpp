#include "LiveMapSubsystem.h"
#include "LiveMapModule.h"

#include "Async/Async.h"
#include "Components/SplineComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "TimerManager.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildablePipeHyper.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "Buildables/FGBuildablePoleBase.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Buildables/FGBuildableWall.h"
#include "Buildables/FGBuildableWire.h"
#include "FGBuildableSubsystem.h"
#include "FGClearanceInterface.h"
#include "FGLightweightBuildableSubsystem.h"
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

	const char* CategoryOf(const UClass* C, bool bLightweight)
	{
		if (C->IsChildOf<AFGBuildableManufacturer>() || C->IsChildOf<AFGBuildableResourceExtractorBase>())
		{
			return "production";
		}
		if (C->IsChildOf<AFGBuildableGenerator>() || C->IsChildOf<AFGBuildablePowerPole>() || C->IsChildOf<AFGBuildablePowerStorage>())
		{
			return "power";
		}
		if (C->IsChildOf<AFGBuildableStorage>())
		{
			return "storage";
		}
		if (C->IsChildOf<AFGBuildableConveyorBase>() || C->IsChildOf<AFGBuildableConveyorAttachment>()
			|| C->IsChildOf<AFGBuildablePipelineAttachment>() || C->IsChildOf<AFGBuildablePoleBase>()
			|| C->IsChildOf<AFGBuildableTrainPlatform>())
		{
			return "logistics";
		}
		// Lightweights are foundations, walls, beams, pillars, railings and the like.
		if (bLightweight || C->IsChildOf<AFGBuildableFoundation>() || C->IsChildOf<AFGBuildableWall>())
		{
			return "structure";
		}
		return "other";
	}

	/** Spline sampled about every 10 m, capped so long belts stay cheap. 2D because the map is. */
	livemap::Polyline SampleSpline(const USplineComponent* Spline, int32 MaxSamples)
	{
		livemap::Polyline L;
		if (!Spline)
		{
			return L;
		}
		const float Length = Spline->GetSplineLength();
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length / 1000.f), 1, MaxSamples);
		L.reserve(Samples + 1);
		for (int32 I = 0; I <= Samples; ++I)
		{
			const FVector P = Spline->GetLocationAtDistanceAlongSpline(Length * I / Samples, ESplineCoordinateSpace::World);
			L.push_back({P.X, P.Y});
		}
		return L;
	}

	void AddPlaced(livemap::BuildingSet& Set, uint32 Class, const FTransform& T)
	{
		const FVector P = T.GetLocation();
		Set.Items.push_back({Class, P.X, P.Y, P.Z, T.Rotator().Yaw});
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
	Server = std::make_shared<livemap::Server>();

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
	// Once, a little after load so it doesn't stack on the save's own loading hitch.
	GetWorldTimerManager().SetTimer(TerrainTimer, this, &ALiveMapSubsystem::ExportTerrain, 3.f, false);
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: listening on port %d at %d Hz"), Port, Config.TickHz);
}

void ALiveMapSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DynamicTimer);
	GetWorldTimerManager().ClearTimer(StaticTimer);
	GetWorldTimerManager().ClearTimer(TerrainTimer);
	for (FLiveMapStaticJob& Job : StaticJobs)
	{
		Job.Version.Wait(); // they hold the server; don't leave them running past world teardown
	}
	StaticJobs.Empty();
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
	PollStaticJobs();
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
	RescanRails();
	RescanBuildings();
}

void ALiveMapSubsystem::RescanRails()
{
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

const livemap::BuildingClass& ALiveMapSubsystem::DescribeClass(UClass* Class, const AFGBuildable* Sample, const FBox* SampleBox)
{
	if (const livemap::BuildingClass* Known = ClassCache.Find(Class))
	{
		return *Known;
	}
	livemap::BuildingClass& C = ClassCache.Add(Class);
	const AFGBuildable* Cdo = Class->GetDefaultObject<AFGBuildable>();
	C.Name = ToUtf8(Class->GetName());
	C.Display = Cdo ? ToUtf8(Cdo->mDisplayName.ToString()) : std::string();
	C.Category = CategoryOf(Class, Sample == nullptr);

	// Footprint in actor-local space, best source first:
	//  1. foundation dimensions (exact),
	//  2. the class's build clearance (what the game itself treats as its footprint),
	//  3. the bounds of a placed sample's meshes,
	//  4. a 2 m marker.
	FBox Box(ForceInit);
	if (const AFGBuildableFoundation* F = Cast<AFGBuildableFoundation>(Cdo); F && F->mWidth > 0 && F->mDepth > 0)
	{
		Box = FBox(FVector(-F->mWidth / 2, -F->mDepth / 2, 0), FVector(F->mWidth / 2, F->mDepth / 2, 0));
	}
	if (!Box.IsValid && Cdo)
	{
		TArray<FFGClearanceData> Clearance;
		IFGClearanceInterface::Execute_GetClearanceData(const_cast<AFGBuildable*>(Cdo), Clearance);
		for (const FFGClearanceData& D : Clearance)
		{
			if (D.IsValid() && D.Type != EClearanceType::CT_Soft)
			{
				Box += D.GetTransformedClearanceBox();
			}
		}
	}
	if (!Box.IsValid && SampleBox && SampleBox->IsValid)
	{
		Box = *SampleBox;
	}
	if (!Box.IsValid && Sample)
	{
		Box = Sample->CalculateComponentsBoundingBoxInLocalSpace(false, false);
	}
	if (!Box.IsValid || Box.GetSize().X > 100000 || Box.GetSize().Y > 100000)
	{
		Box = FBox(FVector(-100, -100, 0), FVector(100, 100, 0));
	}
	C.MinX = Box.Min.X;
	C.MinY = Box.Min.Y;
	C.MaxX = Box.Max.X;
	C.MaxY = Box.Max.Y;
	return C;
}

void ALiveMapSubsystem::RescanBuildings()
{
	if (StaticJobs.Num() > 0)
	{
		return; // previous encode still running; pick changes up next rescan
	}
	const double T0 = FPlatformTime::Seconds();
	UWorld* World = GetWorld();

	livemap::BuildingSet Set;
	livemap::LineSet Lines;
	std::vector<livemap::Polyline>& Belts = Lines["belts"];
	std::vector<livemap::Polyline>& Pipes = Lines["pipes"];
	std::vector<livemap::Polyline>& Tubes = Lines["hypertubes"];
	std::vector<livemap::Polyline>& Power = Lines["power"];
	TMap<const UClass*, uint32> Index;
	TMap<FName, livemap::Polyline> NextSplineCache;

	auto AddSpline = [&](std::vector<livemap::Polyline>& Out, const AActor* Actor, const USplineComponent* Spline)
	{
		const FName Key = Actor->GetFName();
		livemap::Polyline* Hit = SplineCache.Find(Key);
		livemap::Polyline L = Hit ? MoveTemp(*Hit) : SampleSpline(Spline, 16);
		Out.push_back(L);
		NextSplineCache.Add(Key, MoveTemp(L));
	};

	auto IndexOf = [&](UClass* Class, const AFGBuildable* Sample, const FBox* SampleBox) -> uint32
	{
		if (const uint32* I = Index.Find(Class))
		{
			return *I;
		}
		const uint32 I = static_cast<uint32>(Set.Classes.size());
		Set.Classes.push_back(DescribeClass(Class, Sample, SampleBox));
		Index.Add(Class, I);
		return I;
	};

	auto Visit = [&](AFGBuildable* B)
	{
		// Temporaries are actors the game spawns for a lightweight you're looking
		// at; the lightweight pass below already has them.
		if (!B || B->GetIsLightweightTemporary() || B->IsA<AFGBuildableRailroadTrack>())
		{
			return;
		}
		if (const AFGBuildableConveyorBelt* Belt = Cast<AFGBuildableConveyorBelt>(B))
		{
			AddSpline(Belts, Belt, Belt->GetSplineComponent());
		}
		else if (const AFGBuildablePipeline* Pipe = Cast<AFGBuildablePipeline>(B))
		{
			AddSpline(Pipes, Pipe, Pipe->GetSplineComponent());
		}
		else if (const AFGBuildablePipeHyper* Tube = Cast<AFGBuildablePipeHyper>(B))
		{
			AddSpline(Tubes, Tube, Tube->GetSplineComponent());
		}
		else if (const AFGBuildableWire* Wire = Cast<AFGBuildableWire>(B))
		{
			const FVector A = Wire->GetConnectionLocation(0), Z = Wire->GetConnectionLocation(1);
			Power.push_back({{A.X, A.Y}, {Z.X, Z.Y}});
		}
		else
		{
			AddPlaced(Set, IndexOf(B->GetClass(), B, nullptr), B->GetActorTransform());
		}
	};

	if (AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World))
	{
		for (AFGBuildable* B : Buildables->GetAllBuildablesRef())
		{
			Visit(B);
		}
	}
	else
	{
		for (TActorIterator<AFGBuildable> It(World); It; ++It)
		{
			Visit(*It);
		}
	}

	int32 Lightweights = 0;
	if (AFGLightweightBuildableSubsystem* LW = AFGLightweightBuildableSubsystem::Get(World))
	{
		for (const auto& Pair : LW->GetAllLightweightBuildableInstances())
		{
			UClass* Class = Pair.Key.Get();
			if (!Class)
			{
				continue;
			}
			for (const FRuntimeBuildableInstanceData& Inst : Pair.Value)
			{
				// Removed instances stay in the array as empty slots.
				if (!Inst.IsValid())
				{
					continue;
				}
				AddPlaced(Set, IndexOf(Class, nullptr, &Inst.BoundingBox), Inst.Transform);
				++Lightweights;
			}
		}
	}
	SplineCache = MoveTemp(NextSplineCache); // drops dismantled ones
	const double T1 = FPlatformTime::Seconds();

	const uint64 BuildingsHash = livemap::HashBuildings(Set);
	const uint64 LinesHash = livemap::HashLines(Lines);
	const bool bBuildings = BuildingsHash != LastBuildingsHash;
	const bool bLines = LinesHash != LastLinesHash;
	LastBuildingsHash = BuildingsHash;
	LastLinesHash = LinesHash;

	UE_LOG(LogLiveMap, Verbose, TEXT("LiveMap: scanned %d buildings (%d lightweight), %d belts, %d pipes in %.1f ms"),
		static_cast<int32>(Set.Items.size()), Lightweights, static_cast<int32>(Belts.size()), static_cast<int32>(Pipes.size()),
		(T1 - T0) * 1000.0);
	if (!bBuildings && !bLines)
	{
		return;
	}
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: buildings changed: %d pieces (%d lightweight, %d classes), %d belts, %d pipes, %d hypertubes, %d wires; scan %.1f ms"),
		static_cast<int32>(Set.Items.size()), Lightweights, static_cast<int32>(Set.Classes.size()), static_cast<int32>(Belts.size()),
		static_cast<int32>(Pipes.size()), static_cast<int32>(Tubes.size()), static_cast<int32>(Power.size()), (T1 - T0) * 1000.0);

	// Encoding a late-game save takes tens of ms, so it happens off the game thread.
	std::shared_ptr<livemap::Server> S = Server;
	if (bBuildings)
	{
		StaticJobs.Add({"buildings", Async(EAsyncExecution::ThreadPool, [S, Doc = MoveTemp(Set)]() -> uint64
		{
			return S->SetStatic("buildings", livemap::EncodeBuildings(Doc));
		})});
	}
	if (bLines)
	{
		StaticJobs.Add({"lines", Async(EAsyncExecution::ThreadPool, [S, Doc = MoveTemp(Lines)]() -> uint64
		{
			return S->SetStatic("lines", livemap::EncodeLines(Doc));
		})});
	}
}

void ALiveMapSubsystem::PollStaticJobs()
{
	bool bAny = false;
	for (int32 I = StaticJobs.Num() - 1; I >= 0; --I)
	{
		if (StaticJobs[I].Version.IsReady())
		{
			// 0 means the job failed and already logged why; there's nothing new to announce.
			if (const uint64 Version = StaticJobs[I].Version.Get())
			{
				Differ->SetStaticVersion(StaticJobs[I].Name, Version);
				bAny = true;
			}
			StaticJobs.RemoveAtSwap(I);
		}
	}
	if (bAny)
	{
		// The new versions ride in the snapshot; clients refetch what changed.
		Server->Publish(Differ->Snapshot(), std::string(), Differ->Seq());
		Server->RequestResync();
	}
}

void ALiveMapSubsystem::ExportTerrain()
{
	if (!Server)
	{
		return;
	}
	if (!FApp::CanEverRender())
	{
		// Dedicated servers have no GPU to draw with. TODO: decode the mips on the CPU there.
		UE_LOG(LogLiveMap, Log, TEXT("LiveMap: no terrain on a server that can't render"));
		return;
	}
	const double T0 = FPlatformTime::Seconds();

	// The map screen's material (Material_UI_MapFog) samples four slices,
	// Map_<column>-<row>, that together cover the playable world bounds. We use
	// the player's own install at runtime, so no Coffee Stain art ships with the mod.
	UTexture2D* Slices[2][2] = {};
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			const FString Path = FString::Printf(TEXT("/Game/FactoryGame/Interface/UI/Assets/MapTest/SlicedMap/Map_%d-%d.Map_%d-%d"), X, Y, X, Y);
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
			if (!Tex)
			{
				UE_LOG(LogLiveMap, Warning, TEXT("LiveMap: no terrain, could not load %s"), *Path);
				return;
			}
			// Cooked textures stream their big mips in on demand; we want the top one.
			Tex->SetForceMipLevelsToBeResident(30.f);
			Tex->WaitForStreaming();
			UE_LOG(LogLiveMap, Log, TEXT("LiveMap: terrain slice %d-%d is %dx%d"), X, Y, Tex->GetSizeX(), Tex->GetSizeY());
			Slices[X][Y] = Tex;
		}
	}

	// Cooked textures have no CPU copy, so let the GPU draw them into a render
	// target and read that back.
	const int32 Size = FMath::Clamp(TerrainSize, 512, 8192);
	const float Half = Size / 2.f;
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size, RTF_RGBA8_SRGB, FLinearColor::Black, false);
	if (!Target)
	{
		UE_LOG(LogLiveMap, Warning, TEXT("LiveMap: no terrain, could not create a %dx%d render target"), Size, Size);
		return;
	}
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, CanvasSize, Context);
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			Canvas->K2_DrawTexture(Slices[X][Y], FVector2D(X * Half, Y * Half), FVector2D(Half, Half),
				FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor::White, BLEND_Opaque);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Size * Size)
	{
		UE_LOG(LogLiveMap, Warning, TEXT("LiveMap: no terrain, reading the render target back failed"));
		return;
	}
	Target->ReleaseResource();
	for (FColor& C : Pixels)
	{
		C.A = 255;
	}
	const double T1 = FPlatformTime::Seconds();

	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Jpeg = Images.CreateImageWrapper(EImageFormat::JPEG);
	std::shared_ptr<livemap::Server> S = Server;
	StaticJobs.Add({"map", Async(EAsyncExecution::ThreadPool, [S, Jpeg, Pixels = MoveTemp(Pixels), Size]() -> uint64
	{
		if (!Jpeg || !Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8))
		{
			UE_LOG(LogLiveMap, Warning, TEXT("LiveMap: no terrain, JPEG encoding failed"));
			return 0;
		}
		const TArray64<uint8>& Out = Jpeg->GetCompressed(85);
		UE_LOG(LogLiveMap, Log, TEXT("LiveMap: terrain image %dx%d, %lld KB"), Size, Size, Out.Num() / 1024);
		return S->SetStatic("map", std::string(reinterpret_cast<const char*>(Out.GetData()), static_cast<size_t>(Out.Num())), "image/jpeg");
	})});
	UE_LOG(LogLiveMap, Log, TEXT("LiveMap: terrain rendered in %.0f ms on the game thread"), (T1 - T0) * 1000.0);
}
