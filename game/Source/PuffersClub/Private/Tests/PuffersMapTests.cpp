// World map tests: geometry, pack JSON, the shipped world layout, routing, bridge/tunnel splitting and build sanity checks.
// Run: Session Frontend > Automation > Puffers.Map, or -ExecCmds="Automation RunTests Puffers.Map"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Map/PuffersMapData.h"
#include "Map/PuffersMapBuildService.h"
#include "Map/PuffersMapReplicator.h"
#include "Map/PuffersMapSurveyor.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PUFFERS_MAP_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace
{
	FPuffersMapRoad MakeRoad(const TCHAR* Id, std::initializer_list<FVector2D> Pts, EPuffersRoadClass Class = EPuffersRoadClass::Street, uint8 Flags = 0)
	{
		FPuffersMapRoad R;
		R.Id = Id;
		R.Class = Class;
		R.Flags = Flags;
		for (const FVector2D& P : Pts)
		{
			R.Points.Add(FVector(P.X, P.Y, 0.0));
		}
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapGeometryTest, "Puffers.Map.Geometry", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapGeometryTest::RunTest(const FString&)
{
	const TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(10, 0), FVector2D(10, 10), FVector2D(0, 10) };
	TArray<int32> Tris;
	TestTrue(TEXT("square triangulates"), PuffersMapGeo::Triangulate(Square, Tris));
	TestEqual(TEXT("two triangles"), Tris.Num(), 6);
	TestTrue(TEXT("inside"), PuffersMapGeo::PointInPolygon(FVector2D(5, 5), Square));
	TestFalse(TEXT("outside"), PuffersMapGeo::PointInPolygon(FVector2D(15, 5), Square));
	TestEqual(TEXT("area"), FMath::Abs(PuffersMapGeo::SignedArea(Square)), 100.0);

	// An L shape (concave) still triangulates into n-2 triangles.
	const TArray<FVector2D> L = { FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 10), FVector2D(10, 10), FVector2D(10, 20), FVector2D(0, 20) };
	TestTrue(TEXT("L triangulates"), PuffersMapGeo::Triangulate(L, Tris));
	TestEqual(TEXT("L triangles"), Tris.Num(), 12);

	const TArray<FVector> Line = { FVector(0, 0, 0), FVector(50, 0.5, 0), FVector(100, 0, 0), FVector(100, 100, 0) };
	const TArray<FVector> Simple = PuffersMapGeo::Simplify(Line, 2.0);
	TestEqual(TEXT("simplify drops the near-straight point"), Simple.Num(), 3);
	TestEqual(TEXT("length"), PuffersMapGeo::PolylineLength(Line), FVector2D::Distance(FVector2D(0, 0), FVector2D(50, 0.5)) + FVector2D::Distance(FVector2D(50, 0.5), FVector2D(100, 0)) + 100.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapPackJsonTest, "Puffers.Map.PackJson", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapPackJsonTest::RunTest(const FString&)
{
	FPuffersMapPack Pack;
	Pack.Source = TEXT("Test");
	Pack.Space = TEXT("world");
	Pack.Roads.Add(MakeRoad(TEXT("R1"), { FVector2D(0, 0), FVector2D(10000, 0) }, EPuffersRoadClass::Highway, EPuffersRoadFlags::Bridge | EPuffersRoadFlags::OneWay));
	Pack.Roads[0].Name = TEXT("Coast Hwy");
	FPuffersMapArea A;
	A.Id = TEXT("A1");
	A.Kind = EPuffersMapAreaKind::Building;
	A.Polygon = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100) };
	Pack.Areas.Add(A);

	FPuffersMapPack Back;
	FString Error;
	TestTrue(TEXT("parses"), Back.FromJson(Pack.ToJson(), Error));
	TestEqual(TEXT("space"), Back.Space, FString(TEXT("world")));
	TestEqual(TEXT("roads"), Back.Roads.Num(), 1);
	TestEqual(TEXT("name"), Back.Roads[0].Name, FString(TEXT("Coast Hwy")));
	TestEqual(TEXT("class"), (int32)Back.Roads[0].Class, (int32)EPuffersRoadClass::Highway);
	TestTrue(TEXT("bridge flag"), Back.Roads[0].IsBridge());
	TestTrue(TEXT("one way flag"), (Back.Roads[0].Flags & EPuffersRoadFlags::OneWay) != 0);
	TestEqual(TEXT("areas"), Back.Areas.Num(), 1);
	TestEqual(TEXT("area kind"), (int32)Back.Areas[0].Kind, (int32)EPuffersMapAreaKind::Building);

	// World packs convert to map space (cm -> m around the origin).
	FPuffersMapDataSet D;
	D.WorldToMap = [](const FVector2D& W) { return FVector2D(10000.0, 8000.0) + W / 100.0; };
	D.AddPack(Back);
	D.Finalize();
	TestTrue(TEXT("converted"), D.Roads[0].Points.Last().Equals(FVector(10100.0, 8000.0, 0.0), 0.01));

	// Re-adding the same source replaces instead of duplicating.
	D.AddPack(Back);
	D.Finalize();
	TestEqual(TEXT("no duplicates"), D.Roads.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapWorldLayoutTest, "Puffers.Map.WorldLayout", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapWorldLayoutTest::RunTest(const FString&)
{
	FString Json, Error;
	if (!TestTrue(TEXT("WorldLayout.json exists"), FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/Map/WorldLayout.json")))))
	{
		return false;
	}
	FPuffersMapPack Pack;
	TestTrue(TEXT("parses"), Pack.FromJson(Json, Error));
	FPuffersMapDataSet D;
	D.AddPack(Pack);
	D.Finalize();
	const int32 Central = D.FindAreaAt(FVector2D(10000, 8000), EPuffersMapAreaKind::County);
	TestTrue(TEXT("Downtown is in a county"), Central != INDEX_NONE);
	if (Central != INDEX_NONE)
	{
		TestEqual(TEXT("Downtown is Central"), D.Areas[Central].StyleKey, FName(TEXT("central")));
	}
	TestTrue(TEXT("Palm Key is a lot zone"), D.FindAreaAt(FVector2D(19600, 10000), EPuffersMapAreaKind::LotZone) != INDEX_NONE);
	TestTrue(TEXT("open sea is not land"), D.FindAreaAt(FVector2D(21500, 15500), EPuffersMapAreaKind::Land) == INDEX_NONE);
	TestTrue(TEXT("has planned highways"), D.Roads.Num() >= 10);
	TestTrue(TEXT("has places"), D.Labels.Num() >= 60);
	TArray<FVector2D> Route;
	double Len = 0.0;
	TestTrue(TEXT("route from downtown toward the Pen"), D.FindRoute(FVector2D(7600, 8100), FVector2D(1200, 6200), Route, Len));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapRouteTest, "Puffers.Map.Route", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapRouteTest::RunTest(const FString&)
{
	// A square block: the long way round is shorter in time on the highway side.
	FPuffersMapPack Pack;
	Pack.Source = TEXT("Grid");
	Pack.Roads.Add(MakeRoad(TEXT("S1"), { FVector2D(0, 0), FVector2D(1000, 0) }));
	Pack.Roads.Add(MakeRoad(TEXT("S2"), { FVector2D(1000, 0), FVector2D(1000, 1000) }));
	Pack.Roads.Add(MakeRoad(TEXT("S3"), { FVector2D(0, 0), FVector2D(0, 1000) }));
	Pack.Roads.Add(MakeRoad(TEXT("S4"), { FVector2D(0, 1000), FVector2D(1000, 1000) }));
	// One-way the wrong direction: must not be used.
	Pack.Roads.Add(MakeRoad(TEXT("OW"), { FVector2D(1000, 1000), FVector2D(0, 0) }, EPuffersRoadClass::Highway, EPuffersRoadFlags::OneWay));
	FPuffersMapDataSet D;
	D.AddPack(Pack);
	D.Finalize();
	TArray<FVector2D> Route;
	double Len = 0.0;
	TestTrue(TEXT("found"), D.FindRoute(FVector2D(0, 0), FVector2D(1000, 1000), Route, Len));
	TestEqual(TEXT("two sides, not the one-way diagonal"), Len, 2000.0, 1.0);
	TestTrue(TEXT("diagonal used the right way"), D.FindRoute(FVector2D(1000, 1000), FVector2D(0, 0), Route, Len) && Len < 1500.0);
	TestFalse(TEXT("too far from any road"), D.FindRoute(FVector2D(0, 0), FVector2D(9000, 9000), Route, Len, 100.0));
	TestEqual(TEXT("nearest road"), D.FindNearestRoad(FVector2D(500, 20), 50.0, false), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapStructureTest, "Puffers.Map.BridgesAndTunnels", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapStructureTest::RunTest(const FString&)
{
	APuffersMapSurveyor::FSampledRoad S;
	S.Road = MakeRoad(TEXT("R"), { FVector2D(0, 0), FVector2D(20000, 0) });
	S.Samples = APuffersMapSurveyor::Resample(S.Road.Points, 1000.0);
	TestEqual(TEXT("resampled every 10 m"), S.Samples.Num(), 21);
	S.SampleFlags.SetNumZeroed(S.Samples.Num());
	// 50 m raised over water in the middle -> bridge. 10 m covered near the end -> too short, ignored.
	for (int32 i = 6; i <= 11; ++i) S.SampleFlags[i] = APuffersMapSurveyor::Raised | APuffersMapSurveyor::Water;
	S.SampleFlags[17] = APuffersMapSurveyor::Covered;
	TArray<FPuffersMapRoad> Out;
	TMap<FString, int32> Stats;
	APuffersMapSurveyor::SplitByStructures(S, 2500.0, Out, Stats);
	TestEqual(TEXT("three parts"), Out.Num(), 3);
	TestEqual(TEXT("one bridge"), Stats.FindRef(TEXT("bridges")), 1);
	TestEqual(TEXT("no tunnel"), Stats.FindRef(TEXT("tunnels")), 0);
	if (Out.Num() == 3)
	{
		TestTrue(TEXT("middle is the bridge"), Out[1].IsBridge());
		TestFalse(TEXT("ends are roads"), Out[0].IsBridge() || Out[2].IsBridge());
		TestTrue(TEXT("parts connect"), Out[0].Points.Last().Equals(Out[1].Points[0]) || FVector::Dist(Out[0].Points.Last(), Out[1].Points[1]) < 1.0);
	}
	// A forced tunnel stays even when short.
	S.SampleFlags.Init(0, S.Samples.Num());
	S.SampleFlags[3] = APuffersMapSurveyor::ForceTunnel;
	Out.Reset();
	Stats.Reset();
	APuffersMapSurveyor::SplitByStructures(S, 2500.0, Out, Stats);
	TestEqual(TEXT("forced tunnel kept"), Stats.FindRef(TEXT("tunnels")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersMapBuildRulesTest, "Puffers.Map.Builds", PUFFERS_MAP_TEST_FLAGS)
bool FPuffersMapBuildRulesTest::RunTest(const FString&)
{
	FPuffersMapBuild B;
	B.BuildId = TEXT("PLOT_1/HOUSE");
	B.PlotId = TEXT("PLOT_1");
	B.Use = TEXT("Home");
	B.Footprint = PuffersMapGeo::BoxFootprint(FVector2D(4000, 2000), FVector2D(10, 8), 30.0);
	FString Why;
	TestTrue(TEXT("normal house passes"), UPuffersMapBuildService::Sanitize(B, Why));

	FPuffersMapBuild Huge = B;
	Huge.Footprint = PuffersMapGeo::BoxFootprint(FVector2D(4000, 2000), FVector2D(500, 500), 0.0);
	TestFalse(TEXT("1 km² footprint rejected"), UPuffersMapBuildService::Sanitize(Huge, Why));

	FPuffersMapBuild NoId = B;
	NoId.BuildId = NAME_None;
	TestFalse(TEXT("needs an id"), UPuffersMapBuildService::Sanitize(NoId, Why));

	// JSON round trip used by the backend and the offline save.
	B.Seq = 42;
	B.State = EPuffersMapBuildState::ForSale;
	FPuffersMapBuild Back;
	TestTrue(TEXT("json back"), UPuffersMapBuildService::BuildFromJson(UPuffersMapBuildService::BuildToJson(B), Back));
	TestTrue(TEXT("same content"), APuffersMapReplicator::SameContent(B, Back));
	TestEqual(TEXT("seq"), Back.Seq, (int64)42);
	Back.State = EPuffersMapBuildState::Completed;
	TestFalse(TEXT("state change detected"), APuffersMapReplicator::SameContent(B, Back));
	return true;
}

#endif
