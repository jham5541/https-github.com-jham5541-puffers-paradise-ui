// Maps every road, path, tunnel and bridge in the loaded level and (optionally) captures top-down satellite tiles.
//
//   puffers.Map.Survey                      roads from ZoneGraph (City Sample traffic + pedestrian lanes), landscape splines,
//                                           tagged spline actors and road/sidewalk/building meshes; bridges and tunnels detected
//   puffers.Map.Survey satellite            ... and capture top-down tiles (needs a GPU that can render the level)
//   puffers.Map.Survey bounds=core          limit to the 4 x 4 km Puffington Core (or bounds=minX,minY,maxX,maxY in map meters)
//   puffers.Map.Survey nosweep              only what's loaded now (no World Partition sweep)
//
// Run it in PIE or a development build on each level (City Sample, L_Outskirts, every new environment). It writes
// Content/Data/Map/Generated/<Level>.mappack.json (and Satellite/<Level>/), replacing that level's previous pack, so
// adding an environment is: open it, run the survey, commit the pack. The map reloads packs automatically.
//
// World Partition levels are swept cell by cell: the surveyor becomes the view target so cells stream in around it,
// then it reads what loaded and runs the bridge/tunnel traces while the geometry is there.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Map/PuffersMapTypes.h"
#include "Map/PuffersMapData.h"
#include "PuffersMapSurveyor.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPuffersMapSubsystem;

UCLASS(NotPlaceable, Transient)
class PUFFERSCLUB_API APuffersMapSurveyor : public AActor
{
	GENERATED_BODY()

public:
	APuffersMapSurveyor();

	struct FOptions
	{
		bool bSatellite = false;
		bool bSweep = true;
		// Map-space bounds (meters). Invalid = everything the level has.
		FBox2D Bounds = FBox2D(ForceInit);
		FString PackName;
	};

	void Start(const FOptions& InOptions);
	virtual void Tick(float DeltaSeconds) override;

	// Exposed for tests: splits a resampled road into runs by per-sample flags.
	struct FSampledRoad
	{
		FPuffersMapRoad Road;      // world-space points (cm)
		TArray<FVector> Samples;   // resampled points (cm)
		TArray<uint8> SampleFlags; // ESample bits
	};
	enum ESample : uint8
	{
		Raised = 1 << 0,
		Covered = 1 << 1,
		Water = 1 << 2,
		Classified = 1 << 3,
		ForceBridge = 1 << 4,
		ForceTunnel = 1 << 5,
		Exclude = 1 << 6,
	};
	static TArray<FVector> Resample(const TArray<FVector>& Points, double StepCm);
	static void SplitByStructures(const FSampledRoad& In, double MinLengthCm, TArray<FPuffersMapRoad>& Out, TMap<FString, int32>& Stats);

private:
	enum class EPhase : uint8 { Idle, Collect, Sweep, Write, Done };

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviousViewTarget;

	FOptions Options;
	EPhase Phase = EPhase::Idle;
	FPuffersMapConfig Config;
	TWeakObjectPtr<UPuffersMapSubsystem> Map;

	TArray<FSampledRoad> Roads;
	TArray<FPuffersMapArea> Areas;
	TMap<FString, int32> Stats;
	TSet<FString> SeenActors;

	// Sweep grid (world cm), one cell per satellite tile.
	FBox2D SweepWorld = FBox2D(ForceInit);
	double CellCm = 50000.0;
	int32 Cols = 0;
	int32 Rows = 0;
	int32 CellIndex = 0;
	bool bMovedToCell = false;
	double WaitUntil = 0.0;
	double StartedAt = 0.0;
	FString SatelliteDir;
	TArray<TSharedPtr<class FJsonValue>> TileManifest;

	void CollectZoneGraph();
	void CollectLoadedActors();
	void ClassifyInBox(const FBox2D& WorldBox);
	void ClassifySample(const FSampledRoad& Road, int32 SampleIndex, uint8& OutFlags) const;
	void ApplyTagVolumes();
	void CaptureCell(int32 X, int32 Y, const FBox2D& CellBox);
	void BuildPyramid();
	void WriteResults();
	void Finish();

	FBox2D CellBounds(int32 X, int32 Y) const;
	bool IsStreamingDone() const;
	void AddRoad(FPuffersMapRoad&& Road);
	bool InBounds(const FVector& WorldPoint) const;
	const FPuffersMapSurveyRule* MatchRule(const TArray<FPuffersMapSurveyRule>& Rules, const TArray<FString>& Names) const;
};
