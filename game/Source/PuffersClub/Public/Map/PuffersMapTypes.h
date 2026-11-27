// World map data types.
//
// Map space: meters, +X east, +Y south (screen down), origin at the top-left corner of the 22 x 16 km world box.
// World space: Unreal centimeters. UPuffersMapSubsystem::WorldToMap / MapToWorld convert using Content/Data/Map/MapConfig.json.
//
// Generated packs (from the surveyor) store world-space points so a change to the world origin never needs a re-survey.
// Hand-made layout (counties, water, places) is authored in map space.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "PuffersMapTypes.generated.h"

PUFFERSCLUB_API DECLARE_LOG_CATEGORY_EXTERN(LogPuffersMap, Log, All);

class APuffersMapReplicator;
struct FPuffersMapBuildArray;

UENUM(BlueprintType)
enum class EPuffersRoadClass : uint8
{
	Highway,
	Arterial,
	Street,
	Alley,
	// Sidewalks, crosswalks, plazas, stairs.
	Path,
	// Dirt roads and hiking trails in the outskirts.
	Trail,
	Rail,
	Ferry,
	// Drawn from the world plan until a survey replaces it.
	Planned,
	MAX UMETA(Hidden)
};

namespace EPuffersRoadFlags
{
	enum Type : uint8
	{
		None = 0,
		Bridge = 1 << 0,
		Tunnel = 1 << 1,
		OneWay = 1 << 2,
		// A raised road that crosses another road (not water).
		Overpass = 1 << 3,
	};
}

UENUM(BlueprintType)
enum class EPuffersMapAreaKind : uint8
{
	// Draw order follows this list (lower first).
	Land,
	County,
	Park,
	Water,
	District,
	LotZone,
	RoadSurface,
	Sidewalk,
	Plaza,
	Building,
	MAX UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EPuffersMapLabelKind : uint8
{
	County,
	District,
	Place,
	Street,
	Water,
	Landmark
};

UENUM(BlueprintType)
enum class EPuffersMapBuildState : uint8
{
	UnderConstruction,
	Completed,
	// Forced sale after a lapsed plot subscription.
	ForSale,
	// Taken in an inactivity raid; stays on the map until the new owner changes it.
	Raided,
	Demolished
};

UENUM(BlueprintType)
enum class EPuffersMapViewMode : uint8
{
	// Flat vector map (with satellite tiles under it when they've been captured).
	Map2D,
	// The real world from a camera rig above the player, with the map drawn over it.
	World3D
};

// One road, path, rail line or ferry route. Points are in map space after loading (X, Y meters; Z elevation meters).
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMapRoad
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName Id;

	UPROPERTY(BlueprintReadOnly)
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	EPuffersRoadClass Class = EPuffersRoadClass::Street;

	// EPuffersRoadFlags bits.
	UPROPERTY(BlueprintReadOnly)
	uint8 Flags = 0;

	UPROPERTY(BlueprintReadOnly)
	float WidthM = 8.f;

	UPROPERTY(BlueprintReadOnly)
	TArray<FVector> Points;

	// Level or layout file it came from.
	UPROPERTY(BlueprintReadOnly)
	FName Source;

	bool IsBridge() const { return (Flags & EPuffersRoadFlags::Bridge) != 0; }
	bool IsTunnel() const { return (Flags & EPuffersRoadFlags::Tunnel) != 0; }
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMapArea
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName Id;

	UPROPERTY(BlueprintReadOnly)
	FString Name;

	UPROPERTY(BlueprintReadOnly)
	EPuffersMapAreaKind Kind = EPuffersMapAreaKind::Land;

	// County (or style key) used for color.
	UPROPERTY(BlueprintReadOnly)
	FName StyleKey;

	// Outer ring in map space (meters). Holes are separate Water areas drawn on top.
	UPROPERTY(BlueprintReadOnly)
	TArray<FVector2D> Polygon;

	// Triangle indices into Polygon (filled by the loader if missing).
	UPROPERTY()
	TArray<int32> Triangles;

	UPROPERTY(BlueprintReadOnly)
	float HeightM = 0.f;

	UPROPERTY(BlueprintReadOnly)
	FName Source;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMapLabel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString Text;

	UPROPERTY(BlueprintReadOnly)
	EPuffersMapLabelKind Kind = EPuffersMapLabelKind::Place;

	UPROPERTY(BlueprintReadOnly)
	FVector2D Position = FVector2D::ZeroVector;

	// Icon key for places: start, prison, disp, lounge, shop, home, transit.
	UPROPERTY(BlueprintReadOnly)
	FName Icon;

	UPROPERTY(BlueprintReadOnly)
	FName StyleKey;

	UPROPERTY(BlueprintReadOnly)
	FString Note;

	// Hidden below this zoom (pixels per meter) and above MaxZoom.
	UPROPERTY(BlueprintReadOnly)
	float MinZoom = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float MaxZoom = 1000.f;
};

// A satellite (top-down render) tile.
USTRUCT()
struct PUFFERSCLUB_API FPuffersMapTileRef
{
	GENERATED_BODY()

	int32 Level = 0;
	int32 X = 0;
	int32 Y = 0;
	// Map-space rectangle.
	FBox2D Bounds = FBox2D(ForceInit);
	FString File;
};

// A player build as every client sees it on the map.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMapBuild : public FFastArraySerializerItem
{
	GENERATED_BODY()

	// Stable id: "<PlotId>/<BuildingId>" for plot buildings, or any unique id from the build system.
	UPROPERTY(BlueprintReadOnly)
	FName BuildId;

	UPROPERTY(BlueprintReadOnly)
	FName PlotId;

	UPROPERTY(BlueprintReadOnly)
	FName OwnerId;

	UPROPERTY(BlueprintReadOnly)
	FString OwnerName;

	// Home, Shop, Venue, Attraction, Grow, Other.
	UPROPERTY(BlueprintReadOnly)
	FName Use;

	UPROPERTY(BlueprintReadOnly)
	FString Label;

	UPROPERTY(BlueprintReadOnly)
	EPuffersMapBuildState State = EPuffersMapBuildState::UnderConstruction;

	// Footprint in map space (meters), counter-clockwise or clockwise.
	UPROPERTY(BlueprintReadOnly)
	TArray<FVector2D> Footprint;

	UPROPERTY(BlueprintReadOnly)
	float HeightM = 0.f;

	UPROPERTY(BlueprintReadOnly)
	int32 BuildVersion = 1;

	// Backend sequence number (0 = not yet stored). Highest wins on merge.
	UPROPERTY(BlueprintReadOnly)
	int64 Seq = 0;

	// UTC unix seconds of the last change.
	UPROPERTY(BlueprintReadOnly)
	int64 UpdatedAt = 0;

	FVector2D GetCenter() const
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Footprint)
		{
			Sum += P;
		}
		return Footprint.Num() > 0 ? Sum / Footprint.Num() : FVector2D::ZeroVector;
	}

	void PreReplicatedRemove(const FPuffersMapBuildArray& InArraySerializer);
	void PostReplicatedAdd(const FPuffersMapBuildArray& InArraySerializer);
	void PostReplicatedChange(const FPuffersMapBuildArray& InArraySerializer);
};

USTRUCT()
struct PUFFERSCLUB_API FPuffersMapBuildArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FPuffersMapBuild> Items;

	// Not replicated: the actor that owns the array (for client callbacks).
	UPROPERTY(NotReplicated)
	TObjectPtr<APuffersMapReplicator> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FPuffersMapBuild, FPuffersMapBuildArray>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FPuffersMapBuildArray> : public TStructOpsTypeTraitsBase2<FPuffersMapBuildArray>
{
	enum { WithNetDeltaSerializer = true };
};

// A dynamic marker (mission objective, job, player-placed pin).
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersMapMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FName Id;

	UPROPERTY(BlueprintReadWrite)
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite)
	FName Icon = TEXT("objective");

	UPROPERTY(BlueprintReadWrite)
	FString Label;

	UPROPERTY(BlueprintReadWrite)
	FLinearColor Color = FLinearColor(1.f, 0.82f, 0.25f, 1.f);

	// Pulses and shows on the minimap edge when off-screen.
	UPROPERTY(BlueprintReadWrite)
	bool bTracked = false;
};

// Style: colors and widths, loaded from MapConfig.json "style". Every key has a default so a missing entry never breaks the map.
USTRUCT()
struct PUFFERSCLUB_API FPuffersMapRoadStyle
{
	GENERATED_BODY()

	FLinearColor Fill = FLinearColor::White;
	FLinearColor Casing = FLinearColor::Black;
	// Minimum on-screen width (px) and the real width multiplier.
	float MinPx = 1.f;
	float MaxPx = 28.f;
	// Hidden when fewer pixels per meter than this.
	float MinZoom = 0.f;
};

USTRUCT()
struct PUFFERSCLUB_API FPuffersMapStyle
{
	GENERATED_BODY()

	FLinearColor Sea = FLinearColor(0.027f, 0.082f, 0.13f, 1.f);
	FLinearColor SeaGlow = FLinearColor(0.055f, 0.153f, 0.22f, 1.f);
	FLinearColor Land = FLinearColor(0.13f, 0.15f, 0.17f, 1.f);
	FLinearColor Water = FLinearColor(0.06f, 0.18f, 0.27f, 1.f);
	FLinearColor Park = FLinearColor(0.12f, 0.22f, 0.15f, 1.f);
	FLinearColor RoadSurface = FLinearColor(0.32f, 0.34f, 0.37f, 1.f);
	FLinearColor Sidewalk = FLinearColor(0.24f, 0.25f, 0.27f, 1.f);
	FLinearColor BuildingFill = FLinearColor(0.2f, 0.21f, 0.24f, 1.f);
	FLinearColor BuildingEdge = FLinearColor(0.3f, 0.31f, 0.35f, 1.f);
	FLinearColor LotZone = FLinearColor(1.f, 1.f, 1.f, 0.08f);
	FLinearColor Label = FLinearColor(0.93f, 0.95f, 0.97f, 1.f);
	FLinearColor LabelShadow = FLinearColor(0.f, 0.f, 0.f, 0.75f);
	FLinearColor Accent = FLinearColor(1.f, 0.82f, 0.25f, 1.f);
	FLinearColor Route = FLinearColor(0.78f, 0.49f, 1.f, 1.f);
	FLinearColor Panel = FLinearColor(0.03f, 0.05f, 0.07f, 0.82f);
	FLinearColor BridgeCasing = FLinearColor(0.02f, 0.02f, 0.03f, 1.f);
	float TunnelAlpha = 0.45f;
	TMap<FName, FLinearColor> CountyColors;
	TMap<FName, FLinearColor> CountyLand;
	TMap<FName, FLinearColor> UseColors;
	TMap<FName, FLinearColor> IconColors;
	FPuffersMapRoadStyle Roads[(int32)EPuffersRoadClass::MAX];

	FPuffersMapStyle();
	const FPuffersMapRoadStyle& Road(EPuffersRoadClass Class) const { return Roads[FMath::Clamp((int32)Class, 0, (int32)EPuffersRoadClass::MAX - 1)]; }
};

// How the surveyor maps ZoneGraph tags, spline actors and mesh names to road classes.
USTRUCT()
struct PUFFERSCLUB_API FPuffersMapSurveyRule
{
	GENERATED_BODY()

	// Substring matched (case-insensitive) against a ZoneGraph tag, actor tag, class or mesh name.
	FString Match;
	EPuffersRoadClass Class = EPuffersRoadClass::Street;
	// If set, produces an area instead of a road.
	bool bArea = false;
	EPuffersMapAreaKind AreaKind = EPuffersMapAreaKind::RoadSurface;
	uint8 Flags = 0;
	// Skip (e.g. "Intersection" zones are already covered by the roads through them).
	bool bIgnore = false;
};

USTRUCT()
struct PUFFERSCLUB_API FPuffersMapConfig
{
	GENERATED_BODY()

	// World box in map meters.
	FVector2D WorldSizeM = FVector2D(22000.0, 16000.0);
	// Where Unreal's world origin (0,0) sits in map space. Downtown Central Station.
	FVector2D WorldOriginOnMapM = FVector2D(10000.0, 8000.0);
	// Rotation of world +X relative to map east, degrees clockwise.
	float WorldYawDeg = 0.f;

	// Zoom limits (pixels per meter).
	float MinZoom = 0.02f;
	float MaxZoom = 6.f;
	float DefaultZoom = 0.25f;
	float MinimapZoom = 0.9f;

	// Survey rules, tested in order; first match wins.
	TArray<FPuffersMapSurveyRule> ZoneTagRules;
	TArray<FPuffersMapSurveyRule> ActorRules;
	TArray<FPuffersMapSurveyRule> MeshRules;

	// Bridge/tunnel detection.
	float SampleStepM = 10.f;
	float BridgeDropM = 5.f;
	float TunnelCoverM = 14.f;
	float MinStructureLengthM = 25.f;

	// Survey and satellite capture sweep.
	float SweepCellM = 400.f;
	float SweepWaitSeconds = 2.5f;
	float SatelliteTileM = 500.f;
	int32 SatelliteTilePx = 1024;
	int32 SatelliteJpegQuality = 88;

	// Online build sync. Empty URL = local only.
	FString BackendUrl;
	float BackendPollSeconds = 5.f;

	FPuffersMapStyle Style;
};
