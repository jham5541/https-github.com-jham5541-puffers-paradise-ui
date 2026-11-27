#include "Map/PuffersMapSurveyor.h"

#include "Map/PuffersMapRoadTagVolume.h"
#include "Map/PuffersMapSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "ZoneGraphData.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphTypes.h"

namespace
{
	float DefaultWidthM(EPuffersRoadClass C)
	{
		switch (C)
		{
		case EPuffersRoadClass::Highway: return 14.f;
		case EPuffersRoadClass::Arterial: return 11.f;
		case EPuffersRoadClass::Street: return 8.f;
		case EPuffersRoadClass::Alley: return 4.f;
		case EPuffersRoadClass::Path: return 2.5f;
		case EPuffersRoadClass::Trail: return 3.f;
		case EPuffersRoadClass::Rail: return 3.f;
		default: return 6.f;
		}
	}

	bool IsVehicleClass(EPuffersRoadClass C)
	{
		return C == EPuffersRoadClass::Highway || C == EPuffersRoadClass::Arterial || C == EPuffersRoadClass::Street || C == EPuffersRoadClass::Alley;
	}
}

APuffersMapSurveyor::APuffersMapSurveyor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetReplicates(false);
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	RootComponent = Capture;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
}

// ------------------------------------------------------------------------------------------------ Static helpers

TArray<FVector> APuffersMapSurveyor::Resample(const TArray<FVector>& Points, double StepCm)
{
	TArray<FVector> Out;
	if (Points.Num() == 0)
	{
		return Out;
	}
	Out.Add(Points[0]);
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		const FVector A = Points[i - 1];
		const FVector B = Points[i];
		const double Len = FVector::Dist2D(A, B);
		const int32 Extra = StepCm > 0.0 ? FMath::FloorToInt(Len / StepCm) : 0;
		for (int32 k = 1; k <= Extra; ++k)
		{
			const double T = (k * StepCm) / Len;
			if (T < 0.999)
			{
				Out.Add(FMath::Lerp(A, B, T));
			}
		}
		Out.Add(B);
	}
	return Out;
}

void APuffersMapSurveyor::SplitByStructures(const FSampledRoad& In, double MinLengthCm, TArray<FPuffersMapRoad>& Out, TMap<FString, int32>& Stats)
{
	const int32 N = In.Samples.Num();
	if (N < 2)
	{
		return;
	}
	// Per-sample structure flag (before the minimum-length filter).
	TArray<uint8> Raw;
	Raw.SetNumZeroed(N);
	for (int32 i = 0; i < N; ++i)
	{
		const uint8 F = In.SampleFlags.IsValidIndex(i) ? In.SampleFlags[i] : 0;
		if (F & Exclude)
		{
			Raw[i] = 0xFF;
		}
		else if ((F & ForceTunnel) || (F & Covered))
		{
			Raw[i] = EPuffersRoadFlags::Tunnel;
		}
		else if ((F & ForceBridge) || (F & Raised))
		{
			Raw[i] = ((F & Water) || (F & ForceBridge)) ? EPuffersRoadFlags::Bridge : EPuffersRoadFlags::Overpass;
		}
	}
	// Drop automatic runs shorter than the minimum (overhangs, signs, curbs). Forced runs always stay.
	int32 Start = 0;
	while (Start < N)
	{
		int32 End = Start;
		while (End + 1 < N && Raw[End + 1] == Raw[Start])
		{
			++End;
		}
		if (Raw[Start] != 0 && Raw[Start] != 0xFF)
		{
			double Len = 0.0;
			bool bForced = false;
			for (int32 i = Start; i <= End; ++i)
			{
				if (i > Start) Len += FVector::Dist2D(In.Samples[i - 1], In.Samples[i]);
				bForced |= (In.SampleFlags[i] & (ForceBridge | ForceTunnel)) != 0;
			}
			if (!bForced && Len < MinLengthCm)
			{
				for (int32 i = Start; i <= End; ++i)
				{
					Raw[i] = 0;
				}
			}
		}
		Start = End + 1;
	}
	// Merge neighbors that became equal, then emit one road per run (sharing the boundary point).
	Start = 0;
	int32 Part = 0;
	while (Start < N)
	{
		int32 End = Start;
		while (End + 1 < N && Raw[End + 1] == Raw[Start])
		{
			++End;
		}
		const uint8 Flag = Raw[Start];
		if (Flag != 0xFF)
		{
			FPuffersMapRoad R = In.Road;
			R.Points.Reset();
			const int32 From = FMath::Max(Start - 1, 0);
			for (int32 i = From; i <= End; ++i)
			{
				R.Points.Add(In.Samples[i]);
			}
			if (End + 1 < N)
			{
				R.Points.Add(In.Samples[End + 1]);
			}
			R.Flags = (In.Road.Flags & ~(EPuffersRoadFlags::Bridge | EPuffersRoadFlags::Tunnel | EPuffersRoadFlags::Overpass)) | Flag;
			R.Flags |= In.Road.Flags & (EPuffersRoadFlags::Bridge | EPuffersRoadFlags::Tunnel);
			if (Part > 0 || End + 1 < N)
			{
				R.Id = FName(*FString::Printf(TEXT("%s_%d"), *In.Road.Id.ToString(), Part));
			}
			if (R.Points.Num() >= 2)
			{
				if (Flag & EPuffersRoadFlags::Bridge) Stats.FindOrAdd(TEXT("bridges"))++;
				if (Flag & EPuffersRoadFlags::Tunnel) Stats.FindOrAdd(TEXT("tunnels"))++;
				if (Flag & EPuffersRoadFlags::Overpass) Stats.FindOrAdd(TEXT("overpasses"))++;
				Out.Add(MoveTemp(R));
			}
			++Part;
		}
		Start = End + 1;
	}
}

// ------------------------------------------------------------------------------------------------ Start

const FPuffersMapSurveyRule* APuffersMapSurveyor::MatchRule(const TArray<FPuffersMapSurveyRule>& Rules, const TArray<FString>& Names) const
{
	for (const FPuffersMapSurveyRule& Rule : Rules)
	{
		for (const FString& N : Names)
		{
			if (N.Contains(Rule.Match, ESearchCase::IgnoreCase))
			{
				return &Rule;
			}
		}
	}
	return nullptr;
}

bool APuffersMapSurveyor::InBounds(const FVector& WorldPoint) const
{
	if (!Options.Bounds.bIsValid || !Map.IsValid())
	{
		return true;
	}
	return Options.Bounds.IsInside(Map->WorldToMap(WorldPoint));
}

void APuffersMapSurveyor::AddRoad(FPuffersMapRoad&& Road)
{
	if (Road.Points.Num() < 2)
	{
		return;
	}
	int32 Inside = 0;
	for (const FVector& P : Road.Points)
	{
		Inside += InBounds(P) ? 1 : 0;
	}
	if (Inside == 0)
	{
		return;
	}
	FSampledRoad& S = Roads.AddDefaulted_GetRef();
	S.Samples = Resample(Road.Points, Config.SampleStepM * 100.0);
	S.SampleFlags.SetNumZeroed(S.Samples.Num());
	S.Road = MoveTemp(Road);
	Stats.FindOrAdd(FString::Printf(TEXT("class:%s"), *PuffersMapNames::RoadClassName(S.Road.Class)))++;
}

void APuffersMapSurveyor::Start(const FOptions& InOptions)
{
	Options = InOptions;
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	Map = GI ? GI->GetSubsystem<UPuffersMapSubsystem>() : nullptr;
	if (!Map.IsValid())
	{
		UE_LOG(LogPuffersMap, Error, TEXT("Survey needs UPuffersMapSubsystem."));
		Destroy();
		return;
	}
	Config = Map->GetConfig();
	if (Options.PackName.IsEmpty())
	{
		Options.PackName = GetWorld()->GetMapName();
		Options.PackName.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	}
	StartedAt = FPlatformTime::Seconds();
	Phase = EPhase::Collect;
	UE_LOG(LogPuffersMap, Display, TEXT("Survey of %s started (satellite %s, sweep %s)."), *Options.PackName,
		Options.bSatellite ? TEXT("on") : TEXT("off"), Options.bSweep ? TEXT("on") : TEXT("off"));
}

// ------------------------------------------------------------------------------------------------ Collect

void APuffersMapSurveyor::CollectZoneGraph()
{
	UZoneGraphSubsystem* ZGS = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (!ZGS)
	{
		Stats.Add(TEXT("zonegraph:none"), 1);
		return;
	}
	const TConstArrayView<FZoneGraphTagInfo> TagInfos = ZGS->GetTagInfos();
	auto TagNames = [&TagInfos](const FZoneGraphTagMask& Mask)
	{
		TArray<FString> Names;
		for (const FZoneGraphTagInfo& Info : TagInfos)
		{
			if (Info.IsValid() && Mask.Contains(Info.Tag))
			{
				Names.Add(Info.Name.ToString());
			}
		}
		return Names;
	};

	int32 DataIndex = 0;
	for (const FRegisteredZoneGraphData& Reg : ZGS->GetRegisteredZoneGraphData())
	{
		if (!Reg.bInUse || !Reg.ZoneGraphData)
		{
			continue;
		}
		const FZoneGraphStorage& Storage = Reg.ZoneGraphData->GetStorage();
		for (int32 Z = 0; Z < Storage.Zones.Num(); ++Z)
		{
			const FZoneData& Zone = Storage.Zones[Z];
			const TArray<FString> ZoneTags = TagNames(Zone.Tags);
			for (const FString& T : ZoneTags)
			{
				Stats.FindOrAdd(TEXT("zonetag:") + T)++;
			}
			const FPuffersMapSurveyRule* ZoneRule = MatchRule(Config.ZoneTagRules, ZoneTags);
			if (ZoneRule && ZoneRule->bIgnore)
			{
				continue;
			}
			const EPuffersRoadClass ZoneClass = ZoneRule ? ZoneRule->Class : EPuffersRoadClass::Street;

			// The zone outline is the real road (or sidewalk) surface: draw it when zoomed in.
			if (Zone.BoundaryPointsEnd - Zone.BoundaryPointsBegin >= 3)
			{
				FPuffersMapArea A;
				A.Id = FName(*FString::Printf(TEXT("ZG%d_Z%d"), DataIndex, Z));
				A.Kind = (ZoneRule && ZoneRule->bArea) ? ZoneRule->AreaKind
					: (ZoneClass == EPuffersRoadClass::Path ? EPuffersMapAreaKind::Sidewalk : EPuffersMapAreaKind::RoadSurface);
				bool bInside = false;
				for (int32 P = Zone.BoundaryPointsBegin; P < Zone.BoundaryPointsEnd; ++P)
				{
					const FVector& V = Storage.BoundaryPoints[P];
					A.Polygon.Add(FVector2D(V.X, V.Y));
					bInside |= InBounds(V);
				}
				if (bInside)
				{
					Areas.Add(MoveTemp(A));
				}
			}
			if (ZoneRule && ZoneRule->bArea)
			{
				continue;
			}
			for (int32 L = Zone.LanesBegin; L < Zone.LanesEnd; ++L)
			{
				const FZoneLaneData& Lane = Storage.Lanes[L];
				const TArray<FString> LaneTags = TagNames(Lane.Tags);
				const FPuffersMapSurveyRule* LaneRule = MatchRule(Config.ZoneTagRules, LaneTags);
				if (LaneRule && LaneRule->bIgnore)
				{
					continue;
				}
				FPuffersMapRoad R;
				R.Id = FName(*FString::Printf(TEXT("ZG%d_L%d"), DataIndex, L));
				R.Class = LaneRule ? LaneRule->Class : ZoneClass;
				R.Flags = (LaneRule ? LaneRule->Flags : 0) | (ZoneRule ? ZoneRule->Flags : 0);
				// Traffic lanes run one way; the graph follows ZoneGraph's own lane direction.
				if (IsVehicleClass(R.Class))
				{
					R.Flags |= EPuffersRoadFlags::OneWay;
				}
				R.WidthM = Lane.Width > 0.f ? Lane.Width / 100.f : DefaultWidthM(R.Class);
				// A zone with many lanes is a bigger road: promote streets to arterials.
				if (R.Class == EPuffersRoadClass::Street && (Zone.LanesEnd - Zone.LanesBegin) >= 5)
				{
					R.Class = EPuffersRoadClass::Arterial;
				}
				for (int32 P = Lane.PointsBegin; P < Lane.PointsEnd; ++P)
				{
					R.Points.Add(Storage.LanePoints[P]);
				}
				AddRoad(MoveTemp(R));
			}
		}
		++DataIndex;
	}
	Stats.Add(TEXT("zonegraph:datas"), DataIndex);
}

void APuffersMapSurveyor::CollectLoadedActors()
{
	UWorld* World = GetWorld();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this || Actor->IsA<APawn>())
		{
			continue;
		}
		const FString Path = Actor->GetPathName();
		if (SeenActors.Contains(Path))
		{
			continue;
		}
		SeenActors.Add(Path);

		TArray<FString> Names = { Actor->GetName(), Actor->GetClass()->GetName() };
#if WITH_EDITOR
		Names.Add(Actor->GetActorLabel());
#endif
		for (const FName& Tag : Actor->Tags)
		{
			Names.Add(Tag.ToString());
		}
		const FPuffersMapSurveyRule* ActorRule = MatchRule(Config.ActorRules, Names);

		// Landscape splines (outskirts roads and trails).
		TArray<ULandscapeSplinesComponent*> LandscapeSplines;
		Actor->GetComponents(LandscapeSplines);
		for (ULandscapeSplinesComponent* Splines : LandscapeSplines)
		{
			if (ActorRule && ActorRule->bIgnore)
			{
				break;
			}
			const FTransform& T = Splines->GetComponentTransform();
			int32 SegIndex = 0;
			for (const TObjectPtr<ULandscapeSplineSegment>& Segment : Splines->GetSegments())
			{
				if (!Segment)
				{
					continue;
				}
				FPuffersMapRoad R;
				R.Id = FName(*FString::Printf(TEXT("LS_%s_%d"), *Actor->GetName(), SegIndex++));
				R.Class = ActorRule ? ActorRule->Class : EPuffersRoadClass::Street;
				R.Flags = ActorRule ? ActorRule->Flags : 0;
				double Width = 0.0;
				for (const FLandscapeSplineInterpPoint& P : Segment->GetPoints())
				{
					R.Points.Add(T.TransformPosition(P.Center));
					Width = FMath::Max(Width, FVector::Dist(P.Left, P.Right));
				}
				R.WidthM = Width > 0.0 ? (float)(Width / 100.0) : DefaultWidthM(R.Class);
				AddRoad(MoveTemp(R));
				Stats.FindOrAdd(TEXT("source:landscapeSpline"))++;
			}
		}

		// Spline actors that match a rule (road tools, rail lines, custom paths).
		if (ActorRule && !ActorRule->bIgnore && !ActorRule->bArea)
		{
			TArray<USplineComponent*> Splines;
			Actor->GetComponents(Splines);
			int32 SplineIndex = 0;
			for (USplineComponent* Spline : Splines)
			{
				const float Len = Spline->GetSplineLength();
				if (Len < 100.f)
				{
					continue;
				}
				FPuffersMapRoad R;
				R.Id = FName(*FString::Printf(TEXT("SP_%s_%d"), *Actor->GetName(), SplineIndex++));
				R.Class = ActorRule->Class;
				R.Flags = ActorRule->Flags;
				R.WidthM = DefaultWidthM(R.Class);
				for (float D = 0.f; D < Len; D += 500.f)
				{
					R.Points.Add(Spline->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World));
				}
				R.Points.Add(Spline->GetLocationAtDistanceAlongSpline(Len, ESplineCoordinateSpace::World));
				AddRoad(MoveTemp(R));
				Stats.FindOrAdd(TEXT("source:spline"))++;
			}
		}

		// Meshes named like roads, sidewalks, bridges, buildings: footprints as areas.
		if (Config.MeshRules.Num() == 0)
		{
			continue;
		}
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			const UStaticMesh* Asset = Mesh ? Mesh->GetStaticMesh() : nullptr;
			if (!Asset)
			{
				continue;
			}
			const FPuffersMapSurveyRule* Rule = MatchRule(Config.MeshRules, { Asset->GetName() });
			if (!Rule || Rule->bIgnore || !Rule->bArea)
			{
				continue;
			}
			const FBox LocalBox = Asset->GetBoundingBox();
			auto AddFootprint = [&](const FTransform& T, int32 Instance)
			{
				const FVector C[4] = {
					T.TransformPosition(FVector(LocalBox.Min.X, LocalBox.Min.Y, 0)), T.TransformPosition(FVector(LocalBox.Max.X, LocalBox.Min.Y, 0)),
					T.TransformPosition(FVector(LocalBox.Max.X, LocalBox.Max.Y, 0)), T.TransformPosition(FVector(LocalBox.Min.X, LocalBox.Max.Y, 0)) };
				if (!InBounds(C[0]) || Areas.Num() > 400000)
				{
					return;
				}
				FPuffersMapArea A;
				A.Id = FName(*FString::Printf(TEXT("M_%s_%d"), *Mesh->GetName(), Instance));
				A.Kind = Rule->AreaKind;
				for (const FVector& V : C)
				{
					A.Polygon.Add(FVector2D(V.X, V.Y));
				}
				A.HeightM = (float)((LocalBox.Max.Z - LocalBox.Min.Z) * T.GetScale3D().Z);
				Areas.Add(MoveTemp(A));
				Stats.FindOrAdd(TEXT("area:") + PuffersMapNames::AreaKindName(Rule->AreaKind))++;
			};
			if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Mesh))
			{
				for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
				{
					FTransform T;
					if (ISM->GetInstanceTransform(i, T, true))
					{
						AddFootprint(T, i);
					}
				}
			}
			else
			{
				AddFootprint(Mesh->GetComponentTransform(), 0);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ Bridges and tunnels

void APuffersMapSurveyor::ClassifySample(const FSampledRoad& Road, int32 i, uint8& OutFlags) const
{
	UWorld* World = GetWorld();
	const FVector P = Road.Samples[i];
	const FVector Prev = Road.Samples[FMath::Max(i - 1, 0)];
	const FVector Next = Road.Samples[FMath::Min(i + 1, Road.Samples.Num() - 1)];
	FVector Dir = (Next - Prev).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const FVector N(-Dir.Y, Dir.X, 0.0);
	const double Side = Road.Road.WidthM * 50.0 + 1200.0;
	const double Drop = Config.BridgeDropM * 100.0;

	FCollisionQueryParams Q(SCENE_QUERY_STAT(PuffersMapSurvey), true);
	Q.AddIgnoredActor(this);
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		Q.AddIgnoredActor(PC->GetPawn());
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);

	// Ground on both sides, traced down from just above road level.
	int32 LowSides = 0;
	bool bWater = false;
	for (int32 S = -1; S <= 1; S += 2)
	{
		const FVector L = P + N * (Side * S);
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByObjectType(Hit, L + FVector(0, 0, 300), L - FVector(0, 0, 30000), Objects, Q);
		if (!bHit || Hit.ImpactPoint.Z < P.Z - Drop)
		{
			++LowSides;
		}
		if (!bHit || (Hit.GetActor() && Hit.GetActor()->GetClass()->GetName().Contains(TEXT("Water"))))
		{
			bWater = true;
		}
	}
	if (LowSides == 2)
	{
		OutFlags |= Raised;
		if (bWater)
		{
			OutFlags |= Water;
		}
	}

	// Something solid overhead, and over both sides too = inside a tunnel (not just under a sign or an overpass edge).
	auto CoveredAt = [&](const FVector& At)
	{
		FHitResult Hit;
		return World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 250), At + FVector(0, 0, Config.TunnelCoverM * 100.0), Objects, Q);
	};
	if (CoveredAt(P) && CoveredAt(P + N * (Road.Road.WidthM * 50.0 + 200.0)) && CoveredAt(P - N * (Road.Road.WidthM * 50.0 + 200.0)))
	{
		OutFlags |= Covered;
	}
}

void APuffersMapSurveyor::ClassifyInBox(const FBox2D& WorldBox)
{
	for (FSampledRoad& R : Roads)
	{
		for (int32 i = 0; i < R.Samples.Num(); ++i)
		{
			uint8& F = R.SampleFlags[i];
			if ((F & Classified) || !WorldBox.IsInside(FVector2D(R.Samples[i])))
			{
				continue;
			}
			ClassifySample(R, i, F);
			F |= Classified;
		}
	}
}

void APuffersMapSurveyor::ApplyTagVolumes()
{
	TArray<APuffersMapRoadTagVolume*> Volumes;
	for (TActorIterator<APuffersMapRoadTagVolume> It(GetWorld()); It; ++It)
	{
		Volumes.Add(*It);
	}
	Stats.Add(TEXT("tagVolumes"), Volumes.Num());
	if (Volumes.Num() == 0)
	{
		return;
	}
	for (FSampledRoad& R : Roads)
	{
		const FVector Mid = R.Samples[R.Samples.Num() / 2];
		for (const APuffersMapRoadTagVolume* V : Volumes)
		{
			if (V->ContainsWorldPoint(Mid))
			{
				if (!V->RoadName.IsEmpty()) R.Road.Name = V->RoadName;
				if (V->bOverrideClass) R.Road.Class = V->RoadClass;
				if (V->bOneWay) R.Road.Flags |= EPuffersRoadFlags::OneWay;
			}
			if (V->bBridge || V->bTunnel || V->bExclude)
			{
				for (int32 i = 0; i < R.Samples.Num(); ++i)
				{
					if (V->ContainsWorldPoint(R.Samples[i]))
					{
						R.SampleFlags[i] |= (V->bBridge ? ForceBridge : 0) | (V->bTunnel ? ForceTunnel : 0) | (V->bExclude ? Exclude : 0);
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ Sweep

FBox2D APuffersMapSurveyor::CellBounds(int32 X, int32 Y) const
{
	const FVector2D Min = SweepWorld.Min + FVector2D(X * CellCm, Y * CellCm);
	return FBox2D(Min, Min + FVector2D(CellCm, CellCm));
}

bool APuffersMapSurveyor::IsStreamingDone() const
{
	const UWorldPartitionSubsystem* WPS = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	return !WPS || WPS->IsStreamingCompleted();
}

void APuffersMapSurveyor::CaptureCell(int32 X, int32 Y, const FBox2D& Cell)
{
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8_SRGB;
		Target->InitAutoFormat(Config.SatelliteTilePx, Config.SatelliteTilePx);
		Target->UpdateResourceImmediate(true);
		Capture->TextureTarget = Target;
		Capture->OrthoWidth = (float)CellCm;
	}
	// Straight down, image top = north (world -Y), image right = east (world +X).
	const FVector2D C = Cell.GetCenter();
	Capture->SetWorldLocationAndRotation(FVector(C.X, C.Y, 2000000.0), FRotator(-90.f, -90.f, 0.f));
	// Twice: the first frame warms up exposure and virtual textures.
	Capture->CaptureScene();
	Capture->CaptureScene();

	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Res = Target->GameThread_GetRenderTargetResource();
	if (!Res || !Res->ReadPixels(Pixels) || Pixels.Num() != Config.SatelliteTilePx * Config.SatelliteTilePx)
	{
		UE_LOG(LogPuffersMap, Warning, TEXT("Satellite capture failed at %d,%d"), X, Y);
		return;
	}
	for (FColor& Px : Pixels)
	{
		Px.A = 255;
	}
	const FString Rel = FString::Printf(TEXT("0/%d_%d.jpg"), X, Y);
	const FImageView Image(Pixels.GetData(), Config.SatelliteTilePx, Config.SatelliteTilePx);
	if (!FImageUtils::SaveImageByExtension(*(SatelliteDir / Rel), Image, Config.SatelliteJpegQuality))
	{
		return;
	}
	TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
	T->SetNumberField(TEXT("l"), 0);
	T->SetNumberField(TEXT("x"), X);
	T->SetNumberField(TEXT("y"), Y);
	T->SetNumberField(TEXT("minX"), Cell.Min.X);
	T->SetNumberField(TEXT("minY"), Cell.Min.Y);
	T->SetNumberField(TEXT("maxX"), Cell.Max.X);
	T->SetNumberField(TEXT("maxY"), Cell.Max.Y);
	T->SetStringField(TEXT("file"), Rel);
	TileManifest.Add(MakeShared<FJsonValueObject>(T));
}

void APuffersMapSurveyor::BuildPyramid()
{
	// Each level halves resolution: 4 child tiles -> 1 parent, box-filtered, until one tile covers everything.
	const int32 Px = Config.SatelliteTilePx;
	int32 LevelCols = Cols;
	int32 LevelRows = Rows;
	for (int32 Level = 1; LevelCols > 1 || LevelRows > 1; ++Level)
	{
		const int32 PCols = (LevelCols + 1) / 2;
		const int32 PRows = (LevelRows + 1) / 2;
		const double Size = CellCm * FMath::Pow(2.0, (double)Level);
		for (int32 Y = 0; Y < PRows; ++Y)
		{
			for (int32 X = 0; X < PCols; ++X)
			{
				TArray<FColor> Out;
				Out.Init(FColor(8, 21, 33, 255), Px * Px);
				bool bAny = false;
				for (int32 Q = 0; Q < 4; ++Q)
				{
					const int32 CX = X * 2 + (Q & 1);
					const int32 CY = Y * 2 + (Q >> 1);
					FImage Child;
					const FString ChildPath = SatelliteDir / FString::Printf(TEXT("%d/%d_%d.jpg"), Level - 1, CX, CY);
					if (!IFileManager::Get().FileExists(*ChildPath) || !FImageUtils::LoadImage(*ChildPath, Child))
					{
						continue;
					}
					Child.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
					if (Child.SizeX != Px || Child.SizeY != Px)
					{
						continue;
					}
					const FColor* Src = (const FColor*)Child.RawData.GetData();
					const int32 OX = (Q & 1) * (Px / 2);
					const int32 OY = (Q >> 1) * (Px / 2);
					for (int32 y = 0; y < Px / 2; ++y)
					{
						for (int32 x = 0; x < Px / 2; ++x)
						{
							const FColor& A = Src[(y * 2) * Px + x * 2];
							const FColor& B = Src[(y * 2) * Px + x * 2 + 1];
							const FColor& C = Src[(y * 2 + 1) * Px + x * 2];
							const FColor& D = Src[(y * 2 + 1) * Px + x * 2 + 1];
							Out[(OY + y) * Px + OX + x] = FColor((A.R + B.R + C.R + D.R) / 4, (A.G + B.G + C.G + D.G) / 4, (A.B + B.B + C.B + D.B) / 4, 255);
						}
					}
					bAny = true;
				}
				if (!bAny)
				{
					continue;
				}
				const FString Rel = FString::Printf(TEXT("%d/%d_%d.jpg"), Level, X, Y);
				FImageUtils::SaveImageByExtension(*(SatelliteDir / Rel), FImageView(Out.GetData(), Px, Px), Config.SatelliteJpegQuality);
				const FVector2D Min = SweepWorld.Min + FVector2D(X * Size, Y * Size);
				TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
				T->SetNumberField(TEXT("l"), Level);
				T->SetNumberField(TEXT("x"), X);
				T->SetNumberField(TEXT("y"), Y);
				T->SetNumberField(TEXT("minX"), Min.X);
				T->SetNumberField(TEXT("minY"), Min.Y);
				T->SetNumberField(TEXT("maxX"), Min.X + Size);
				T->SetNumberField(TEXT("maxY"), Min.Y + Size);
				T->SetStringField(TEXT("file"), Rel);
				TileManifest.Add(MakeShared<FJsonValueObject>(T));
			}
		}
		LevelCols = PCols;
		LevelRows = PRows;
	}
}

// ------------------------------------------------------------------------------------------------ Tick (state machine)

void APuffersMapSurveyor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	switch (Phase)
	{
	case EPhase::Collect:
	{
		CollectZoneGraph();
		CollectLoadedActors();
		// Sweep area: requested bounds, else everything the roads cover (plus a margin), else the world box.
		FBox2D B(ForceInit);
		if (Options.Bounds.bIsValid)
		{
			const FVector W0 = Map->MapToWorld(Options.Bounds.Min);
			const FVector W1 = Map->MapToWorld(Options.Bounds.Max);
			B += FVector2D(W0.X, W0.Y);
			B += FVector2D(W1.X, W1.Y);
		}
		else
		{
			for (const FSampledRoad& R : Roads)
			{
				for (const FVector& P : R.Samples)
				{
					B += FVector2D(P);
				}
			}
			for (const FPuffersMapArea& A : Areas)
			{
				for (const FVector2D& P : A.Polygon)
				{
					B += P;
				}
			}
			if (!B.bIsValid)
			{
				const FVector W0 = Map->MapToWorld(FVector2D::ZeroVector);
				const FVector W1 = Map->MapToWorld(Config.WorldSizeM);
				B += FVector2D(W0.X, W0.Y);
				B += FVector2D(W1.X, W1.Y);
			}
			B = B.ExpandBy(20000.0);
		}
		CellCm = (Options.bSatellite ? Config.SatelliteTileM : Config.SweepCellM) * 100.0;
		SweepWorld = FBox2D(FVector2D(FMath::FloorToDouble(B.Min.X / CellCm) * CellCm, FMath::FloorToDouble(B.Min.Y / CellCm) * CellCm), B.Max);
		Cols = FMath::Max(1, FMath::CeilToInt((SweepWorld.Max.X - SweepWorld.Min.X) / CellCm));
		Rows = FMath::Max(1, FMath::CeilToInt((SweepWorld.Max.Y - SweepWorld.Min.Y) / CellCm));
		CellIndex = 0;
		bMovedToCell = false;

		const bool bPartitioned = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>() != nullptr && GetWorld()->GetWorldPartition() != nullptr;
		if (Options.bSatellite || (Options.bSweep && bPartitioned))
		{
			if (Options.bSatellite)
			{
				SatelliteDir = FPaths::ProjectContentDir() / TEXT("Data/Map/Satellite") / Options.PackName;
				IFileManager::Get().DeleteDirectory(*SatelliteDir, false, true);
				IFileManager::Get().MakeDirectory(*(SatelliteDir / TEXT("0")), true);
			}
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PreviousViewTarget = PC->GetViewTarget();
				PC->SetViewTarget(this);
			}
			UE_LOG(LogPuffersMap, Display, TEXT("Survey sweep: %d x %d cells of %.0f m."), Cols, Rows, CellCm / 100.0);
			Phase = EPhase::Sweep;
		}
		else
		{
			ClassifyInBox(FBox2D(FVector2D(-1e12, -1e12), FVector2D(1e12, 1e12)));
			Phase = EPhase::Write;
		}
		break;
	}
	case EPhase::Sweep:
	{
		if (CellIndex >= Cols * Rows)
		{
			Phase = EPhase::Write;
			break;
		}
		const int32 X = CellIndex % Cols;
		const int32 Y = CellIndex / Cols;
		const FBox2D Cell = CellBounds(X, Y);
		if (!bMovedToCell)
		{
			const FVector2D C = Cell.GetCenter();
			SetActorLocation(FVector(C.X, C.Y, 30000.0));
			WaitUntil = FPlatformTime::Seconds() + Config.SweepWaitSeconds;
			bMovedToCell = true;
			break;
		}
		if (FPlatformTime::Seconds() < WaitUntil || !IsStreamingDone())
		{
			break;
		}
		CollectLoadedActors();
		ClassifyInBox(Cell);
		if (Options.bSatellite)
		{
			CaptureCell(X, Y, Cell);
		}
		++CellIndex;
		bMovedToCell = false;
		if (CellIndex % 10 == 0 || CellIndex == Cols * Rows)
		{
			UE_LOG(LogPuffersMap, Display, TEXT("Survey: %d / %d cells, %d roads so far."), CellIndex, Cols * Rows, Roads.Num());
		}
		break;
	}
	case EPhase::Write:
		WriteResults();
		Finish();
		break;
	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------------ Output

void APuffersMapSurveyor::WriteResults()
{
	ApplyTagVolumes();

	FPuffersMapPack Pack;
	Pack.Source = Options.PackName;
	Pack.Space = TEXT("world");
	Pack.GeneratedAt = FDateTime::UtcNow().ToIso8601();
	Pack.Stats = Stats;
	for (const FSampledRoad& R : Roads)
	{
		// Write the original points for plain roads (smaller); split ones need the samples.
		bool bStructure = false;
		for (uint8 F : R.SampleFlags)
		{
			bStructure |= (F & (Raised | Covered | ForceBridge | ForceTunnel | Exclude)) != 0;
		}
		if (!bStructure)
		{
			Pack.Roads.Add(R.Road);
			continue;
		}
		SplitByStructures(R, Config.MinStructureLengthM * 100.0, Pack.Roads, Pack.Stats);
	}
	Pack.Areas = Areas;
	Pack.Stats.Add(TEXT("roads"), Pack.Roads.Num());
	Pack.Stats.Add(TEXT("areas"), Pack.Areas.Num());
	Pack.Stats.Add(TEXT("seconds"), (int32)(FPlatformTime::Seconds() - StartedAt));

	const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/Map/Generated");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / (Options.PackName + TEXT(".mappack.json"));
	if (FFileHelper::SaveStringToFile(Pack.ToJson(), *File))
	{
		UE_LOG(LogPuffersMap, Display, TEXT("Survey wrote %s: %d roads (%d bridges, %d tunnels, %d overpasses), %d areas."), *File, Pack.Roads.Num(),
			Pack.Stats.FindRef(TEXT("bridges")), Pack.Stats.FindRef(TEXT("tunnels")), Pack.Stats.FindRef(TEXT("overpasses")), Pack.Areas.Num());
	}
	for (const auto& Pair : Stats)
	{
		if (Pair.Key.StartsWith(TEXT("zonetag:")))
		{
			UE_LOG(LogPuffersMap, Display, TEXT("  ZoneGraph tag %s: %d zones"), *Pair.Key.RightChop(8), Pair.Value);
		}
	}

	if (Options.bSatellite && TileManifest.Num() > 0)
	{
		BuildPyramid();
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("source"), Options.PackName);
		Root->SetNumberField(TEXT("tileM"), CellCm / 100.0);
		Root->SetNumberField(TEXT("tilePx"), Config.SatelliteTilePx);
		Root->SetArrayField(TEXT("tiles"), TileManifest);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Json, *(SatelliteDir / TEXT("manifest.json")));
		UE_LOG(LogPuffersMap, Display, TEXT("Satellite: %d tiles in %s"), TileManifest.Num(), *SatelliteDir);
	}
}

void APuffersMapSurveyor::Finish()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		AActor* Back = PreviousViewTarget ? PreviousViewTarget.Get() : PC->GetPawn();
		if (Back)
		{
			PC->SetViewTarget(Back);
		}
	}
	if (Map.IsValid())
	{
		Map->ReloadMapData();
	}
	Phase = EPhase::Done;
	Destroy();
}
