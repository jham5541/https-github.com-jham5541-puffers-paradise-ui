// World map console commands.
//
//   puffers.Map.Toggle                    open / close the full map
//   puffers.Map.Reload                    reload Content/Data/Map (after a survey or a config edit)
//   puffers.Map.Survey [satellite] [nosweep] [bounds=core|minX,minY,maxX,maxY] [name=Pack]
//   puffers.Map.Waypoint <x> <y>          waypoint at a map point (meters)
//   puffers.Map.TestBuild [Use]           records a fake build 30 m in front of you (server/standalone) to check live updates
//   puffers.Map.Where                     where you are on the map

#include "Map/PuffersMapBuildService.h"
#include "Map/PuffersMapSubsystem.h"
#include "Map/PuffersMapSurveyor.h"
#include "Map/PuffersMapData.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	UPuffersMapSubsystem* MapFor(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UPuffersMapSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdToggle(TEXT("puffers.Map.Toggle"), TEXT("Open or close the world map."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UPuffersMapSubsystem* Map = MapFor(World)) Map->ToggleMap();
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdReload(TEXT("puffers.Map.Reload"), TEXT("Reload Content/Data/Map."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UPuffersMapSubsystem* Map = MapFor(World)) Map->ReloadMapData();
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSurvey(TEXT("puffers.Map.Survey"),
		TEXT("Map every road, path, bridge and tunnel in this level. Args: satellite, nosweep, bounds=core|minX,minY,maxX,maxY, name=Pack"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			APuffersMapSurveyor::FOptions Options;
			for (const FString& Arg : Args)
			{
				if (Arg.Equals(TEXT("satellite"), ESearchCase::IgnoreCase)) Options.bSatellite = true;
				else if (Arg.Equals(TEXT("nosweep"), ESearchCase::IgnoreCase)) Options.bSweep = false;
				else if (Arg.StartsWith(TEXT("name="))) Options.PackName = Arg.RightChop(5);
				else if (Arg.StartsWith(TEXT("bounds=")))
				{
					const FString B = Arg.RightChop(7);
					if (B.Equals(TEXT("core"), ESearchCase::IgnoreCase))
					{
						Options.Bounds = FBox2D(FVector2D(8000, 6000), FVector2D(12000, 10000));
					}
					else
					{
						TArray<FString> Parts;
						B.ParseIntoArray(Parts, TEXT(","));
						if (Parts.Num() == 4)
						{
							Options.Bounds = FBox2D(FVector2D(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])), FVector2D(FCString::Atod(*Parts[2]), FCString::Atod(*Parts[3])));
						}
					}
				}
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APuffersMapSurveyor* S = World->SpawnActor<APuffersMapSurveyor>(APuffersMapSurveyor::StaticClass(), FTransform::Identity, Params))
			{
				S->Start(Options);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWaypoint(TEXT("puffers.Map.Waypoint"), TEXT("Set a waypoint at map meters: x y"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UPuffersMapSubsystem* Map = MapFor(World);
			if (Map && Args.Num() == 2)
			{
				Map->SetWaypoint(FVector2D(FCString::Atod(*Args[0]), FCString::Atod(*Args[1])));
				UE_LOG(LogPuffersMap, Display, TEXT("Route: %.0f m"), Map->GetRouteLengthM());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWhere(TEXT("puffers.Map.Where"), TEXT("Where you are on the map."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UPuffersMapSubsystem* Map = MapFor(World);
			FVector2D P;
			float H;
			if (Map && Map->GetPlayerMapPose(P, H))
			{
				UE_LOG(LogPuffersMap, Display, TEXT("Map %.0f, %.0f (heading %.0f): %s"), P.X, P.Y, H, *Map->DescribeLocation(P));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTestBuild(TEXT("puffers.Map.TestBuild"), TEXT("Record a test build in front of you: [Use]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UPuffersMapSubsystem* Map = MapFor(World);
			UPuffersMapBuildService* Service = World ? World->GetSubsystem<UPuffersMapBuildService>() : nullptr;
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Map || !Service || !Pawn)
			{
				return;
			}
			const FVector At = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 3000.0;
			FPuffersMapBuild B;
			B.BuildId = FName(*FString::Printf(TEXT("Test/%lld"), FDateTime::UtcNow().ToUnixTimestamp()));
			B.OwnerId = TEXT("Test");
			B.OwnerName = TEXT("Test Builder");
			B.Use = Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("Home"));
			B.State = EPuffersMapBuildState::Completed;
			B.Footprint = PuffersMapGeo::BoxFootprint(Map->WorldToMap(At), FVector2D(10, 7), Pawn->GetActorRotation().Yaw);
			FString Why;
			if (!Service->RecordBuild(B, Why))
			{
				UE_LOG(LogPuffersMap, Warning, TEXT("Test build rejected: %s"), *Why);
			}
		}));
}
