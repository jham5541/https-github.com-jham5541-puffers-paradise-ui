#include "Map/PuffersMapSubsystem.h"

#include "Map/PuffersMapReplicator.h"
#include "Map/PuffersMapCameraRig.h"
#include "Map/SPuffersMapView.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

static TAutoConsoleVariable<int32> CVarMap3D(TEXT("puffers.Map.3D"), 1,
	TEXT("3D map view: 0 off, 1 auto (off on integrated GPUs), 2 force on."));
static TAutoConsoleVariable<int32> CVarMinimap(TEXT("puffers.Map.Minimap"), 1, TEXT("Show the minimap (0/1)."));

namespace
{
	FString MapDir()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Map");
	}

	FLinearColor Hex(const FString& S, const FLinearColor& Fallback)
	{
		FString H = S.TrimStartAndEnd();
		if (H.IsEmpty())
		{
			return Fallback;
		}
		H.RemoveFromStart(TEXT("#"));
		if (H.Len() != 6 && H.Len() != 8)
		{
			return Fallback;
		}
		return FLinearColor(FColor::FromHex(H));
	}

	FLinearColor Col(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FLinearColor& Fallback)
	{
		FString S;
		return (O.IsValid() && O->TryGetStringField(Key, S)) ? Hex(S, Fallback) : Fallback;
	}

	void ReadColorMap(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, TMap<FName, FLinearColor>& Out)
	{
		const TSharedPtr<FJsonObject>* M = nullptr;
		if (O.IsValid() && O->TryGetObjectField(Key, M))
		{
			for (const auto& Pair : (*M)->Values)
			{
				Out.Add(FName(*Pair.Key), Hex(Pair.Value->AsString(), FLinearColor::White));
			}
		}
	}

	TArray<FPuffersMapSurveyRule> ReadRules(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FPuffersMapSurveyRule> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!O.IsValid() || !O->TryGetArrayField(Key, Arr))
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (!R.IsValid())
			{
				continue;
			}
			FPuffersMapSurveyRule Rule;
			FString S;
			R->TryGetStringField(TEXT("match"), Rule.Match);
			if (R->TryGetStringField(TEXT("class"), S)) Rule.Class = PuffersMapNames::ParseRoadClass(S);
			if (R->TryGetStringField(TEXT("area"), S))
			{
				Rule.bArea = true;
				Rule.AreaKind = PuffersMapNames::ParseAreaKind(S);
			}
			TArray<FString> Flags;
			if (R->TryGetStringArrayField(TEXT("flags"), Flags)) Rule.Flags = PuffersMapNames::ParseRoadFlags(Flags);
			R->TryGetBoolField(TEXT("ignore"), Rule.bIgnore);
			if (!Rule.Match.IsEmpty())
			{
				Out.Add(Rule);
			}
		}
		return Out;
	}
}

FPuffersMapStyle::FPuffersMapStyle()
{
	auto RS = [this](EPuffersRoadClass C, const TCHAR* Fill, const TCHAR* Casing, float MinPx, float MaxPx, float MinZoom)
	{
		FPuffersMapRoadStyle& S = Roads[(int32)C];
		S.Fill = FLinearColor(FColor::FromHex(Fill));
		S.Casing = FLinearColor(FColor::FromHex(Casing));
		S.MinPx = MinPx;
		S.MaxPx = MaxPx;
		S.MinZoom = MinZoom;
	};
	RS(EPuffersRoadClass::Highway, TEXT("FFC94AFF"), TEXT("1A1406FF"), 2.6f, 40.f, 0.f);
	RS(EPuffersRoadClass::Arterial, TEXT("F2F4F7FF"), TEXT("0E1217FF"), 1.8f, 32.f, 0.04f);
	RS(EPuffersRoadClass::Street, TEXT("D5DAE0FF"), TEXT("0E1217FF"), 1.2f, 26.f, 0.12f);
	RS(EPuffersRoadClass::Alley, TEXT("A9B0B8FF"), TEXT("0E1217FF"), 0.8f, 16.f, 0.5f);
	RS(EPuffersRoadClass::Path, TEXT("8E97A3B0"), TEXT("00000000"), 0.6f, 10.f, 0.9f);
	RS(EPuffersRoadClass::Trail, TEXT("C9A876FF"), TEXT("2A2015FF"), 0.9f, 12.f, 0.08f);
	RS(EPuffersRoadClass::Rail, TEXT("9AA3AEFF"), TEXT("1B2026FF"), 1.0f, 10.f, 0.05f);
	RS(EPuffersRoadClass::Ferry, TEXT("FFC94ACC"), TEXT("00000000"), 1.2f, 4.f, 0.f);
	RS(EPuffersRoadClass::Planned, TEXT("FFC94AFF"), TEXT("1A1406FF"), 2.2f, 30.f, 0.f);

	CountyColors.Add(TEXT("central"), FLinearColor(FColor::FromHex(TEXT("E9A23B"))));
	CountyColors.Add(TEXT("heights"), FLinearColor(FColor::FromHex(TEXT("5B8FC7"))));
	CountyColors.Add(TEXT("southside"), FLinearColor(FColor::FromHex(TEXT("D9734E"))));
	CountyColors.Add(TEXT("frontier"), FLinearColor(FColor::FromHex(TEXT("A781C9"))));
	CountyColors.Add(TEXT("coast"), FLinearColor(FColor::FromHex(TEXT("3FB892"))));
	CountyLand.Add(TEXT("central"), FLinearColor(FColor::FromHex(TEXT("3A3226"))));
	CountyLand.Add(TEXT("heights"), FLinearColor(FColor::FromHex(TEXT("25303D"))));
	CountyLand.Add(TEXT("southside"), FLinearColor(FColor::FromHex(TEXT("3A2A25"))));
	CountyLand.Add(TEXT("frontier"), FLinearColor(FColor::FromHex(TEXT("33293D"))));
	CountyLand.Add(TEXT("coast"), FLinearColor(FColor::FromHex(TEXT("213830"))));
	UseColors.Add(TEXT("Home"), FLinearColor(FColor::FromHex(TEXT("FFC94A"))));
	UseColors.Add(TEXT("Shop"), FLinearColor(FColor::FromHex(TEXT("45C6EC"))));
	UseColors.Add(TEXT("Venue"), FLinearColor(FColor::FromHex(TEXT("C77DFF"))));
	UseColors.Add(TEXT("Attraction"), FLinearColor(FColor::FromHex(TEXT("FF7AB6"))));
	UseColors.Add(TEXT("Grow"), FLinearColor(FColor::FromHex(TEXT("7ED957"))));
	IconColors.Add(TEXT("prison"), FLinearColor(FColor::FromHex(TEXT("FF5A4F"))));
	IconColors.Add(TEXT("disp"), FLinearColor(FColor::FromHex(TEXT("7ED957"))));
	IconColors.Add(TEXT("lounge"), FLinearColor(FColor::FromHex(TEXT("C77DFF"))));
	IconColors.Add(TEXT("shop"), FLinearColor(FColor::FromHex(TEXT("45C6EC"))));
	IconColors.Add(TEXT("home"), FLinearColor(FColor::FromHex(TEXT("FFC94A"))));
	IconColors.Add(TEXT("transit"), FLinearColor(FColor::FromHex(TEXT("FFC94A"))));
	IconColors.Add(TEXT("start"), FLinearColor(FColor::FromHex(TEXT("FFD23F"))));
	IconColors.Add(TEXT("jail"), FLinearColor(FColor::FromHex(TEXT("FF8A80"))));
}

// ------------------------------------------------------------------------------------------------ Lifecycle

void UPuffersMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bInitialized = true;
	ReloadMapData();
}

void UPuffersMapSubsystem::Deinitialize()
{
	RemoveWidgets();
	Destroy3DRig();
	bInitialized = false;
	Super::Deinitialize();
}

TStatId UPuffersMapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPuffersMapSubsystem, STATGROUP_Tickables);
}

void UPuffersMapSubsystem::LoadConfig()
{
	Config = FPuffersMapConfig();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *(MapDir() / TEXT("MapConfig.json"))))
	{
		UE_LOG(LogPuffersMap, Warning, TEXT("No Content/Data/Map/MapConfig.json, using defaults."));
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogPuffersMap, Error, TEXT("MapConfig.json is not valid JSON, using defaults."));
		return;
	}
	auto Vec2 = [&Root](const TCHAR* Key, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Root->TryGetArrayField(Key, A) && A->Num() == 2)
		{
			Out = FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
		}
	};
	auto Flt = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float& Out)
	{
		double D;
		if (O.IsValid() && O->TryGetNumberField(Key, D))
		{
			Out = (float)D;
		}
	};
	Vec2(TEXT("worldSizeM"), Config.WorldSizeM);
	Vec2(TEXT("worldOriginOnMapM"), Config.WorldOriginOnMapM);
	Flt(Root, TEXT("worldYawDeg"), Config.WorldYawDeg);

	const TSharedPtr<FJsonObject>* Zoom = nullptr;
	if (Root->TryGetObjectField(TEXT("zoom"), Zoom))
	{
		Flt(*Zoom, TEXT("min"), Config.MinZoom);
		Flt(*Zoom, TEXT("max"), Config.MaxZoom);
		Flt(*Zoom, TEXT("default"), Config.DefaultZoom);
		Flt(*Zoom, TEXT("minimap"), Config.MinimapZoom);
	}

	const TSharedPtr<FJsonObject>* Survey = nullptr;
	if (Root->TryGetObjectField(TEXT("survey"), Survey))
	{
		Config.ZoneTagRules = ReadRules(*Survey, TEXT("zoneGraphTags"));
		Config.ActorRules = ReadRules(*Survey, TEXT("actors"));
		Config.MeshRules = ReadRules(*Survey, TEXT("meshes"));
		Flt(*Survey, TEXT("sampleStepM"), Config.SampleStepM);
		Flt(*Survey, TEXT("bridgeDropM"), Config.BridgeDropM);
		Flt(*Survey, TEXT("tunnelCoverM"), Config.TunnelCoverM);
		Flt(*Survey, TEXT("minStructureLengthM"), Config.MinStructureLengthM);
		Flt(*Survey, TEXT("sweepCellM"), Config.SweepCellM);
		Flt(*Survey, TEXT("sweepWaitSeconds"), Config.SweepWaitSeconds);
	}

	const TSharedPtr<FJsonObject>* Sat = nullptr;
	if (Root->TryGetObjectField(TEXT("satellite"), Sat))
	{
		Flt(*Sat, TEXT("tileM"), Config.SatelliteTileM);
		double D;
		if ((*Sat)->TryGetNumberField(TEXT("tilePx"), D)) Config.SatelliteTilePx = FMath::Clamp((int32)D, 128, 4096);
		if ((*Sat)->TryGetNumberField(TEXT("jpegQuality"), D)) Config.SatelliteJpegQuality = FMath::Clamp((int32)D, 30, 100);
	}

	const TSharedPtr<FJsonObject>* Online = nullptr;
	if (Root->TryGetObjectField(TEXT("online"), Online))
	{
		(*Online)->TryGetStringField(TEXT("backendUrl"), Config.BackendUrl);
		Flt(*Online, TEXT("pollSeconds"), Config.BackendPollSeconds);
	}
	// An environment variable wins so servers can point at staging without editing content.
	const FString EnvUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("PUFFERS_BACKEND_URL"));
	if (!EnvUrl.IsEmpty())
	{
		Config.BackendUrl = EnvUrl;
	}

	const TSharedPtr<FJsonObject>* StyleObj = nullptr;
	if (Root->TryGetObjectField(TEXT("style"), StyleObj))
	{
		const TSharedPtr<FJsonObject>& S = *StyleObj;
		FPuffersMapStyle& St = Config.Style;
		St.Sea = Col(S, TEXT("sea"), St.Sea);
		St.SeaGlow = Col(S, TEXT("seaGlow"), St.SeaGlow);
		St.Land = Col(S, TEXT("land"), St.Land);
		St.Water = Col(S, TEXT("water"), St.Water);
		St.Park = Col(S, TEXT("park"), St.Park);
		St.RoadSurface = Col(S, TEXT("roadSurface"), St.RoadSurface);
		St.Sidewalk = Col(S, TEXT("sidewalk"), St.Sidewalk);
		St.BuildingFill = Col(S, TEXT("buildingFill"), St.BuildingFill);
		St.BuildingEdge = Col(S, TEXT("buildingEdge"), St.BuildingEdge);
		St.LotZone = Col(S, TEXT("lotZone"), St.LotZone);
		St.Label = Col(S, TEXT("label"), St.Label);
		St.Accent = Col(S, TEXT("accent"), St.Accent);
		St.Route = Col(S, TEXT("route"), St.Route);
		St.Panel = Col(S, TEXT("panel"), St.Panel);
		St.BridgeCasing = Col(S, TEXT("bridgeCasing"), St.BridgeCasing);
		Flt(S, TEXT("tunnelAlpha"), St.TunnelAlpha);
		ReadColorMap(S, TEXT("counties"), St.CountyColors);
		ReadColorMap(S, TEXT("countyLand"), St.CountyLand);
		ReadColorMap(S, TEXT("uses"), St.UseColors);
		ReadColorMap(S, TEXT("icons"), St.IconColors);
		const TSharedPtr<FJsonObject>* RoadsObj = nullptr;
		if (S->TryGetObjectField(TEXT("roads"), RoadsObj))
		{
			for (const auto& Pair : (*RoadsObj)->Values)
			{
				const EPuffersRoadClass C = PuffersMapNames::ParseRoadClass(Pair.Key);
				const TSharedPtr<FJsonObject> R = Pair.Value->AsObject();
				FPuffersMapRoadStyle& RS = St.Roads[(int32)C];
				RS.Fill = Col(R, TEXT("fill"), RS.Fill);
				RS.Casing = Col(R, TEXT("casing"), RS.Casing);
				Flt(R, TEXT("minPx"), RS.MinPx);
				Flt(R, TEXT("maxPx"), RS.MaxPx);
				Flt(R, TEXT("minZoom"), RS.MinZoom);
			}
		}
	}
}

void UPuffersMapSubsystem::ReloadMapData()
{
	LoadConfig();
	Data.Reset();
	Data.WorldToMap = [this](const FVector2D& W) { return WorldToMap(FVector(W.X, W.Y, 0.0)); };

	auto LoadPack = [this](const FString& Path)
	{
		FString Json, Error;
		FPuffersMapPack Pack;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			return;
		}
		if (!Pack.FromJson(Json, Error))
		{
			UE_LOG(LogPuffersMap, Error, TEXT("%s: %s"), *Path, *Error);
			return;
		}
		if (Pack.Source.IsEmpty())
		{
			Pack.Source = FPaths::GetBaseFilename(Path);
		}
		Data.AddPack(Pack);
		UE_LOG(LogPuffersMap, Log, TEXT("Map pack %s: %d roads, %d areas, %d labels"), *Pack.Source, Pack.Roads.Num(), Pack.Areas.Num(), Pack.Labels.Num());
	};

	LoadPack(MapDir() / TEXT("WorldLayout.json"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(MapDir() / TEXT("Generated") / TEXT("*.mappack.json")), true, false);
	Files.Sort();
	bool bAnySurveyed = false;
	for (const FString& F : Files)
	{
		LoadPack(MapDir() / TEXT("Generated") / F);
		bAnySurveyed = true;
	}
	// Planned roads from the world plan only show until a real survey exists for that area.
	if (bAnySurveyed)
	{
		FBox2D Surveyed(ForceInit);
		for (const FPuffersMapRoad& R : Data.Roads)
		{
			if (R.Class != EPuffersRoadClass::Planned)
			{
				for (const FVector& P : R.Points)
				{
					Surveyed += FVector2D(P);
				}
			}
		}
		Data.Roads.RemoveAll([&Surveyed](const FPuffersMapRoad& R)
		{
			if (R.Class != EPuffersRoadClass::Planned || !Surveyed.bIsValid)
			{
				return false;
			}
			int32 Inside = 0;
			for (const FVector& P : R.Points)
			{
				Inside += Surveyed.IsInside(FVector2D(P)) ? 1 : 0;
			}
			return Inside * 2 > R.Points.Num();
		});
	}
	Data.Finalize();
	LoadSatelliteManifest();
	++DataVersion;
	UE_LOG(LogPuffersMap, Display, TEXT("World map ready: %d roads, %d areas, %d labels, %d route nodes, %d satellite tiles."),
		Data.Roads.Num(), Data.Areas.Num(), Data.Labels.Num(), Data.NumGraphNodes(), SatelliteTiles.Num());
}

void UPuffersMapSubsystem::LoadSatelliteManifest()
{
	SatelliteTiles.Reset();
	TileTextures.Reset();
	TileLastUse.Reset();
	TileQueue.Reset();
	SatelliteMaxLevel = 0;
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(MapDir() / TEXT("Satellite") / TEXT("*")), false, true);
	for (const FString& Dir : Dirs)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *(MapDir() / TEXT("Satellite") / Dir / TEXT("manifest.json"))))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Tiles = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("tiles"), Tiles))
		{
			continue;
		}
		// Tiles are captured in world space; convert their corners with the current transform.
		for (const TSharedPtr<FJsonValue>& V : *Tiles)
		{
			const TSharedPtr<FJsonObject> T = V->AsObject();
			FPuffersMapTileRef Ref;
			Ref.Level = (int32)T->GetNumberField(TEXT("l"));
			Ref.X = (int32)T->GetNumberField(TEXT("x"));
			Ref.Y = (int32)T->GetNumberField(TEXT("y"));
			const FVector2D A = WorldToMap(FVector(T->GetNumberField(TEXT("minX")), T->GetNumberField(TEXT("minY")), 0));
			const FVector2D B = WorldToMap(FVector(T->GetNumberField(TEXT("maxX")), T->GetNumberField(TEXT("maxY")), 0));
			Ref.Bounds = FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
			Ref.File = MapDir() / TEXT("Satellite") / Dir / T->GetStringField(TEXT("file"));
			SatelliteMaxLevel = FMath::Max(SatelliteMaxLevel, Ref.Level);
			SatelliteTiles.Add(Ref);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Transforms

FVector2D UPuffersMapSubsystem::WorldToMap(const FVector& W) const
{
	const double R = FMath::DegreesToRadians(Config.WorldYawDeg);
	const double C = FMath::Cos(R), S = FMath::Sin(R);
	const double X = W.X / 100.0, Y = W.Y / 100.0;
	return Config.WorldOriginOnMapM + FVector2D(X * C - Y * S, X * S + Y * C);
}

FVector UPuffersMapSubsystem::MapToWorld(const FVector2D& M, float WorldZ) const
{
	const double R = FMath::DegreesToRadians(-Config.WorldYawDeg);
	const double C = FMath::Cos(R), S = FMath::Sin(R);
	const FVector2D D = M - Config.WorldOriginOnMapM;
	return FVector((D.X * C - D.Y * S) * 100.0, (D.X * S + D.Y * C) * 100.0, WorldZ);
}

FName UPuffersMapSubsystem::GetCountyAt(const FVector2D& P) const
{
	const int32 A = Data.FindAreaAt(P, EPuffersMapAreaKind::County);
	return A != INDEX_NONE ? Data.Areas[A].StyleKey : NAME_None;
}

FString UPuffersMapSubsystem::DescribeLocation(const FVector2D& P) const
{
	TArray<FString> Parts;
	const int32 Road = Data.FindNearestRoad(P, 30.0, true);
	if (Road != INDEX_NONE)
	{
		FString Name = Data.Roads[Road].Name;
		if (Data.Roads[Road].IsBridge()) Name += TEXT(" (bridge)");
		if (Data.Roads[Road].IsTunnel()) Name += TEXT(" (tunnel)");
		Parts.Add(Name);
	}
	const int32 District = Data.FindAreaAt(P, EPuffersMapAreaKind::District);
	if (District != INDEX_NONE)
	{
		Parts.Add(Data.Areas[District].Name);
	}
	const int32 Lot = Data.FindAreaAt(P, EPuffersMapAreaKind::LotZone);
	if (Lot != INDEX_NONE)
	{
		Parts.Add(Data.Areas[Lot].Name);
	}
	const int32 County = Data.FindAreaAt(P, EPuffersMapAreaKind::County);
	if (County != INDEX_NONE)
	{
		Parts.Add(Data.Areas[County].Name);
	}
	if (Parts.Num() == 0)
	{
		const int32 Land = Data.FindAreaAt(P, EPuffersMapAreaKind::Land);
		Parts.Add(Land != INDEX_NONE ? TEXT("Open country") : TEXT("Open water"));
	}
	return FString::Join(Parts, TEXT(", "));
}

// ------------------------------------------------------------------------------------------------ Tiles

UTexture2D* UPuffersMapSubsystem::RequestTile(int32 TileIndex)
{
	if (!SatelliteTiles.IsValidIndex(TileIndex))
	{
		return nullptr;
	}
	TileLastUse.Add(TileIndex, FPlatformTime::Seconds());
	if (TObjectPtr<UTexture2D>* Found = TileTextures.Find(TileIndex))
	{
		return Found->Get();
	}
	TileQueue.AddUnique(TileIndex);
	return nullptr;
}

void UPuffersMapSubsystem::PumpTileLoads()
{
	// Newest requests first: they're what the player is looking at now.
	int32 Loaded = 0;
	while (TileQueue.Num() > 0 && Loaded < 2)
	{
		const int32 Index = TileQueue.Pop();
		if (TileTextures.Contains(Index))
		{
			continue;
		}
		UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(SatelliteTiles[Index].File);
		if (Tex)
		{
			Tex->Filter = TF_Bilinear;
			Tex->SRGB = true;
			Tex->UpdateResource();
		}
		TileTextures.Add(Index, Tex);
		++Loaded;
	}
	if (TileQueue.Num() > 64)
	{
		TileQueue.RemoveAt(0, TileQueue.Num() - 64);
	}
	// Keep at most 160 tiles in memory.
	if (TileTextures.Num() > 160)
	{
		TArray<int32> Keys;
		TileTextures.GetKeys(Keys);
		Keys.Sort([this](int32 A, int32 B) { return TileLastUse.FindRef(A) < TileLastUse.FindRef(B); });
		for (int32 i = 0; i < Keys.Num() - 160; ++i)
		{
			TileTextures.Remove(Keys[i]);
		}
	}
}

// ------------------------------------------------------------------------------------------------ Player, markers, route

APlayerController* UPuffersMapSubsystem::GetLocalPlayerController() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetFirstLocalPlayerController() : nullptr;
}

bool UPuffersMapSubsystem::GetPlayerMapPose(FVector2D& OutPos, float& OutHeadingDeg) const
{
	const APlayerController* PC = GetLocalPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}
	OutPos = WorldToMap(Pawn->GetActorLocation());
	OutHeadingDeg = (float)FRotator::NormalizeAxis(Pawn->GetActorRotation().Yaw + Config.WorldYawDeg);
	return true;
}

void UPuffersMapSubsystem::SetMarker(const FPuffersMapMarker& Marker)
{
	FPuffersMapMarker* Existing = Markers.FindByPredicate([&](const FPuffersMapMarker& M) { return M.Id == Marker.Id; });
	if (Existing)
	{
		*Existing = Marker;
	}
	else
	{
		Markers.Add(Marker);
	}
}

void UPuffersMapSubsystem::RemoveMarker(FName MarkerId)
{
	Markers.RemoveAll([MarkerId](const FPuffersMapMarker& M) { return M.Id == MarkerId; });
}

void UPuffersMapSubsystem::SetWaypoint(FVector2D MapPoint)
{
	bHasWaypoint = true;
	Waypoint = MapPoint;
	UpdateRoute(true);
}

void UPuffersMapSubsystem::ClearWaypoint()
{
	bHasWaypoint = false;
	Route.Reset();
	RouteLengthM = 0.0;
}

void UPuffersMapSubsystem::UpdateRoute(bool bForce)
{
	if (!bHasWaypoint)
	{
		return;
	}
	FVector2D Pos;
	float Heading;
	if (!GetPlayerMapPose(Pos, Heading))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bForce && (Now < NextRouteTime || FVector2D::Distance(Pos, RouteFrom) < 30.0))
	{
		return;
	}
	NextRouteTime = Now + 1.5;
	RouteFrom = Pos;
	if (FVector2D::Distance(Pos, Waypoint) < 25.0)
	{
		ClearWaypoint();
		return;
	}
	if (!Data.FindRoute(Pos, Waypoint, Route, RouteLengthM))
	{
		// No road nearby: straight line so the player still knows where to go.
		Route = { Pos, Waypoint };
		RouteLengthM = FVector2D::Distance(Pos, Waypoint);
	}
}

// ------------------------------------------------------------------------------------------------ Builds

APuffersMapReplicator* UPuffersMapSubsystem::FindReplicator() const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<APuffersMapReplicator> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

TArray<FPuffersMapBuild> UPuffersMapSubsystem::GetBuilds() const
{
	const APuffersMapReplicator* Rep = FindReplicator();
	return Rep ? Rep->GetBuilds() : TArray<FPuffersMapBuild>();
}

double UPuffersMapSubsystem::GetBuildAge(FName BuildId) const
{
	const double* At = BuildChangedAt.Find(BuildId);
	return At ? FPlatformTime::Seconds() - *At : 1e6;
}

void UPuffersMapSubsystem::NotifyBuildChanged(const FPuffersMapBuild& Build, bool bRemoved)
{
	if (bRemoved)
	{
		BuildChangedAt.Remove(Build.BuildId);
	}
	else
	{
		BuildChangedAt.Add(Build.BuildId, FPlatformTime::Seconds());
	}
	OnBuildChanged.Broadcast(Build, bRemoved);
}

// ------------------------------------------------------------------------------------------------ Screens

void UPuffersMapSubsystem::EnsureMinimap()
{
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	const bool bWant = bMinimapVisible && CVarMinimap.GetValueOnGameThread() != 0 && !bMapOpen && GetLocalPlayerController() && GetLocalPlayerController()->GetPawn();
	if (bWant && VC && !MiniViewRoot.IsValid())
	{
		MiniView = SNew(SPuffersMapView).MapSubsystem(this).Mode(SPuffersMapView::EMode::Minimap);
		MiniViewRoot = SNew(SBox)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(28.f, 0.f, 0.f, 28.f))
			[
				SNew(SBox).WidthOverride(300.f).HeightOverride(200.f)
				[
					MiniView.ToSharedRef()
				]
			];
		VC->AddViewportWidgetContent(MiniViewRoot.ToSharedRef(), 40);
		ViewportOwner = VC;
	}
	else if (!bWant && MiniViewRoot.IsValid())
	{
		if (UGameViewportClient* Owner = ViewportOwner.Get())
		{
			Owner->RemoveViewportWidgetContent(MiniViewRoot.ToSharedRef());
		}
		MiniViewRoot.Reset();
		MiniView.Reset();
	}
}

void UPuffersMapSubsystem::RemoveWidgets()
{
	UGameViewportClient* Owner = ViewportOwner.Get();
	if (Owner && FullViewRoot.IsValid())
	{
		Owner->RemoveViewportWidgetContent(FullViewRoot.ToSharedRef());
	}
	if (Owner && MiniViewRoot.IsValid())
	{
		Owner->RemoveViewportWidgetContent(MiniViewRoot.ToSharedRef());
	}
	FullViewRoot.Reset();
	FullView.Reset();
	MiniViewRoot.Reset();
	MiniView.Reset();
}

void UPuffersMapSubsystem::OpenMap()
{
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	APlayerController* PC = GetLocalPlayerController();
	if (bMapOpen || !VC || !PC)
	{
		return;
	}
	bMapOpen = true;
	EnsureMinimap();
	FullView = SNew(SPuffersMapView).MapSubsystem(this).Mode(SPuffersMapView::EMode::FullScreen);
	FullViewRoot = FullView;
	VC->AddViewportWidgetContent(FullViewRoot.ToSharedRef(), 60);
	ViewportOwner = VC;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(FullView);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);
	if (ViewMode == EPuffersMapViewMode::World3D)
	{
		FString Why;
		if (Is3DAvailable(Why))
		{
			Spawn3DRig();
		}
		else
		{
			ViewMode = EPuffersMapViewMode::Map2D;
		}
	}
	OnMapOpenChanged.Broadcast(true);
}

void UPuffersMapSubsystem::CloseMap()
{
	if (!bMapOpen)
	{
		return;
	}
	bMapOpen = false;
	if (FullView.IsValid())
	{
		// The view plays its close animation, then asks to be removed.
		TWeakObjectPtr<UPuffersMapSubsystem> WeakThis(this);
		TSharedPtr<SWidget> Root = FullViewRoot;
		TWeakObjectPtr<UGameViewportClient> WeakVC = ViewportOwner;
		FullView->PlayClose(FSimpleDelegate::CreateLambda([WeakThis, Root, WeakVC]()
		{
			if (UGameViewportClient* VC = WeakVC.Get())
			{
				if (Root.IsValid())
				{
					VC->RemoveViewportWidgetContent(Root.ToSharedRef());
				}
			}
		}));
	}
	FullView.Reset();
	FullViewRoot.Reset();
	Destroy3DRig();
	if (APlayerController* PC = GetLocalPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	OnMapOpenChanged.Broadcast(false);
}

void UPuffersMapSubsystem::ToggleMap()
{
	bMapOpen ? CloseMap() : OpenMap();
}

void UPuffersMapSubsystem::SetMinimapVisible(bool bVisible)
{
	bMinimapVisible = bVisible;
	EnsureMinimap();
}

bool UPuffersMapSubsystem::Is3DAvailable(FString& OutWhyNot) const
{
	const int32 Setting = CVarMap3D.GetValueOnGameThread();
	if (Setting == 0)
	{
		OutWhyNot = TEXT("3D map is turned off (puffers.Map.3D 0).");
		return false;
	}
	if (!GetLocalPlayerController())
	{
		OutWhyNot = TEXT("No local player.");
		return false;
	}
	if (Setting == 1 && GRHIDeviceIsIntegrated)
	{
		OutWhyNot = TEXT("3D map needs a dedicated GPU or Apple Silicon. Showing the 2D map. (puffers.Map.3D 2 forces it on.)");
		return false;
	}
	return true;
}

bool UPuffersMapSubsystem::SetViewMode(EPuffersMapViewMode Mode)
{
	if (Mode == EPuffersMapViewMode::World3D)
	{
		FString Why;
		if (!Is3DAvailable(Why))
		{
			if (FullView.IsValid())
			{
				FullView->ShowToast(Why);
			}
			return false;
		}
		ViewMode = Mode;
		SlowFrameSince = 0.0;
		if (bMapOpen)
		{
			Spawn3DRig();
		}
		return true;
	}
	ViewMode = Mode;
	Destroy3DRig();
	return true;
}

void UPuffersMapSubsystem::Spawn3DRig()
{
	APlayerController* PC = GetLocalPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World || CameraRig)
	{
		return;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	CameraRig = World->SpawnActor<APuffersMapCameraRig>(APuffersMapCameraRig::StaticClass(), ViewLoc, ViewRot, Params);
	if (CameraRig)
	{
		CameraRig->Begin(PC, ViewLoc, ViewRot);
	}
}

void UPuffersMapSubsystem::Destroy3DRig()
{
	if (CameraRig)
	{
		CameraRig->End();
		CameraRig = nullptr;
	}
}

// ------------------------------------------------------------------------------------------------ Tick

void UPuffersMapSubsystem::Tick(float DeltaTime)
{
	APlayerController* PC = GetLocalPlayerController();
	if (PC && !bMapOpen && PC->IsLocalController() && PC->WasInputKeyJustPressed(EKeys::M))
	{
		OpenMap();
	}
	EnsureMinimap();
	UpdateRoute(false);
	PumpTileLoads();

	// A 3D view that can't hold ~12 fps for 3 s drops back to 2D.
	if (CameraRig && bMapOpen)
	{
		const double Now = FPlatformTime::Seconds();
		if (DeltaTime > 0.08f)
		{
			if (SlowFrameSince == 0.0)
			{
				SlowFrameSince = Now;
			}
			else if (Now - SlowFrameSince > 3.0 && CVarMap3D.GetValueOnGameThread() != 2)
			{
				SetViewMode(EPuffersMapViewMode::Map2D);
				if (FullView.IsValid())
				{
					FullView->ShowToast(TEXT("3D view was too slow on this machine. Switched to the 2D map."));
				}
			}
		}
		else
		{
			SlowFrameSince = 0.0;
		}
	}
}
