// Map data set: loads map packs (JSON), keeps a spatial grid and level-of-detail copies of every road,
// triangulates areas and answers route queries over the road graph. Plain C++, no world needed, so tests run it directly.

#pragma once

#include "CoreMinimal.h"
#include "Map/PuffersMapTypes.h"

namespace PuffersMapGeo
{
	// Douglas-Peucker on X/Y. Keeps the first and last point.
	PUFFERSCLUB_API TArray<FVector> Simplify(const TArray<FVector>& Points, double Tolerance);
	// Ear clipping. Returns false (and fills a fan) for degenerate rings.
	PUFFERSCLUB_API bool Triangulate(const TArray<FVector2D>& Ring, TArray<int32>& OutTriangles);
	PUFFERSCLUB_API double SignedArea(const TArray<FVector2D>& Ring);
	PUFFERSCLUB_API bool PointInPolygon(const FVector2D& P, const TArray<FVector2D>& Ring);
	PUFFERSCLUB_API double PolylineLength(const TArray<FVector>& Points);
	PUFFERSCLUB_API double DistanceToPolyline(const FVector2D& P, const TArray<FVector>& Points, FVector2D* OutClosest = nullptr);
	// Oriented-box footprint (4 corners) from a center, half extents and yaw (degrees, clockwise in map space).
	PUFFERSCLUB_API TArray<FVector2D> BoxFootprint(const FVector2D& Center, const FVector2D& HalfExtents, double YawDeg);
	// Convex hull (monotone chain).
	PUFFERSCLUB_API TArray<FVector2D> ConvexHull(TArray<FVector2D> Points);
}

namespace PuffersMapNames
{
	PUFFERSCLUB_API FString RoadClassName(EPuffersRoadClass Class);
	PUFFERSCLUB_API EPuffersRoadClass ParseRoadClass(const FString& Name);
	PUFFERSCLUB_API FString AreaKindName(EPuffersMapAreaKind Kind);
	PUFFERSCLUB_API EPuffersMapAreaKind ParseAreaKind(const FString& Name);
	PUFFERSCLUB_API FString LabelKindName(EPuffersMapLabelKind Kind);
	PUFFERSCLUB_API EPuffersMapLabelKind ParseLabelKind(const FString& Name);
	PUFFERSCLUB_API FString BuildStateName(EPuffersMapBuildState State);
	PUFFERSCLUB_API EPuffersMapBuildState ParseBuildState(const FString& Name);
	PUFFERSCLUB_API uint8 ParseRoadFlags(const TArray<FString>& Names);
	PUFFERSCLUB_API TArray<FString> RoadFlagNames(uint8 Flags);
}

// A converted pack before it is merged (the surveyor writes these; the loader reads them).
struct PUFFERSCLUB_API FPuffersMapPack
{
	FString Source;
	FString GeneratedAt;
	// "world" (Unreal cm) or "map" (meters).
	FString Space = TEXT("map");
	TArray<FPuffersMapRoad> Roads;
	TArray<FPuffersMapArea> Areas;
	TArray<FPuffersMapLabel> Labels;
	// Free-form survey stats written for humans (tag counts, bridges found...).
	TMap<FString, int32> Stats;

	bool FromJson(const FString& Json, FString& OutError);
	FString ToJson() const;
};

class PUFFERSCLUB_API FPuffersMapDataSet
{
public:
	static constexpr int32 NumLods = 4;
	// Simplification tolerance (meters) per LOD. Pick with LodForZoom.
	static constexpr double LodTolerance[NumLods] = { 0.0, 1.5, 8.0, 40.0 };

	TArray<FPuffersMapRoad> Roads;
	TArray<FPuffersMapArea> Areas;
	TArray<FPuffersMapLabel> Labels;
	TArray<FString> Sources;

	// Converts a world-space pack to map space. Set by the subsystem from MapConfig.
	TFunction<FVector2D(const FVector2D& WorldCm)> WorldToMap;

	void Reset();
	// Adds a pack. World-space packs are converted with WorldToMap (Z cm -> m). A later pack with the same
	// Source replaces the earlier one, so re-surveying a level never duplicates its roads.
	void AddPack(const FPuffersMapPack& Pack);
	// Builds bounds, LODs, triangles, the spatial grid and the route graph. Call after the last AddPack.
	void Finalize();

	static int32 LodForZoom(double PixelsPerMeter);
	const TArray<FVector>& GetRoadPoints(int32 RoadIndex, int32 Lod) const;
	const FBox2D& GetRoadBounds(int32 RoadIndex) const { return RoadBounds[RoadIndex]; }
	const FBox2D& GetAreaBounds(int32 AreaIndex) const { return AreaBounds[AreaIndex]; }
	FBox2D GetBounds() const { return Bounds; }

	void QueryRoads(const FBox2D& Box, TArray<int32>& Out) const;
	void QueryAreas(const FBox2D& Box, TArray<int32>& Out) const;

	// Nearest named road within MaxDistM (-1 if none).
	int32 FindNearestRoad(const FVector2D& P, double MaxDistM, bool bNamedOnly, double* OutDist = nullptr) const;
	// Smallest area of this kind containing P (-1 if none).
	int32 FindAreaAt(const FVector2D& P, EPuffersMapAreaKind Kind) const;

	// Shortest drive over vehicle roads (A*). Returns false if either end is more than SnapM from a road.
	bool FindRoute(const FVector2D& From, const FVector2D& To, TArray<FVector2D>& OutPath, double& OutLengthM, double SnapM = 400.0) const;

	int32 NumGraphNodes() const { return Nodes.Num(); }

private:
	struct FEdge
	{
		int32 To = INDEX_NONE;
		int32 Road = INDEX_NONE;
		bool bReverse = false;
		float Cost = 0.f;
		float Length = 0.f;
	};

	struct FNode
	{
		FVector2D Pos;
		TArray<FEdge> Edges;
	};

	static constexpr double GridCellM = 500.0;

	TArray<FBox2D> RoadBounds;
	TArray<FBox2D> AreaBounds;
	TArray<TArray<FVector>> RoadLods[NumLods];
	TMap<FIntPoint, TArray<int32>> RoadGrid;
	TMap<FIntPoint, TArray<int32>> AreaGrid;
	FBox2D Bounds = FBox2D(ForceInit);

	TArray<FNode> Nodes;
	TMap<FIntPoint, int32> NodeLookup;

	int32 NodeFor(const FVector2D& P);
	int32 NearestNode(const FVector2D& P, double MaxDistM) const;
	void BuildGraph();
	static void InsertGrid(TMap<FIntPoint, TArray<int32>>& Grid, const FBox2D& Box, int32 Index);
	static void QueryGrid(const TMap<FIntPoint, TArray<int32>>& Grid, const FBox2D& Box, TArray<int32>& Out, int32 Count);
};
