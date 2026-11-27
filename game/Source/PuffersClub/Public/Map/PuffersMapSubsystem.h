// The world map: data, style, satellite tiles, markers, waypoint and the map screens.
//
//   M / Map key        full-screen map (Esc or M closes)
//   in the map         drag pan · wheel zoom · right-click waypoint · F follow · T 2D/3D · L legend · +/- zoom
//   minimap            bottom-left, follows the player (puffers.Map.Minimap 0 hides it)
//
// Data comes from Content/Data/Map:
//   MapConfig.json            world transform, zoom limits, survey rules, style, backend URL
//   WorldLayout.json          counties, land, water, lot zones, places (from the world plan)
//   Generated/*.mappack.json  roads, paths, tunnels, bridges, road surfaces from each surveyed level
//   Satellite/<Level>/        top-down capture tiles + manifest.json (optional)
//
// Player builds come from APuffersMapReplicator (server-authoritative, replicated to every client).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Map/PuffersMapTypes.h"
#include "Map/PuffersMapData.h"
#include "PuffersMapSubsystem.generated.h"

class SPuffersMapView;
class UTexture2D;
class APuffersMapReplicator;
class APuffersMapCameraRig;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPuffersMapBuildChanged, const FPuffersMapBuild&, Build, bool, bRemoved);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuffersMapOpenChanged, bool, bOpen);

UCLASS()
class PUFFERSCLUB_API UPuffersMapSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate() && bInitialized; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	// ---------------------------------------------------------------- Data

	// Reloads config, layout, generated packs and the satellite manifest.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void ReloadMapData();

	const FPuffersMapConfig& GetConfig() const { return Config; }
	const FPuffersMapStyle& GetStyle() const { return Config.Style; }
	const FPuffersMapDataSet& GetData() const { return Data; }
	// Bumped on every reload so views can drop caches.
	int32 GetDataVersion() const { return DataVersion; }

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	FVector2D WorldToMap(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	FVector MapToWorld(const FVector2D& MapPoint, float WorldZ = 0.f) const;

	// "Kush Ave, Southside, Southside County" for a map point.
	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	FString DescribeLocation(const FVector2D& MapPoint) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	FName GetCountyAt(const FVector2D& MapPoint) const;

	// ---------------------------------------------------------------- Satellite tiles

	bool HasSatellite() const { return SatelliteTiles.Num() > 0; }
	const TArray<FPuffersMapTileRef>& GetSatelliteTiles() const { return SatelliteTiles; }
	int32 GetSatelliteMaxLevel() const { return SatelliteMaxLevel; }
	// Returns the texture if loaded; otherwise queues it (a few load per frame) and returns null.
	UTexture2D* RequestTile(int32 TileIndex);

	// ---------------------------------------------------------------- Player, markers, waypoint

	// Local player position in map space, heading in degrees (0 = east, clockwise).
	bool GetPlayerMapPose(FVector2D& OutPos, float& OutHeadingDeg) const;

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void SetMarker(const FPuffersMapMarker& Marker);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void RemoveMarker(FName MarkerId);

	const TArray<FPuffersMapMarker>& GetMarkers() const { return Markers; }

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void SetWaypoint(FVector2D MapPoint);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void ClearWaypoint();

	bool HasWaypoint() const { return bHasWaypoint; }
	FVector2D GetWaypoint() const { return Waypoint; }
	const TArray<FVector2D>& GetRoute() const { return Route; }
	double GetRouteLengthM() const { return RouteLengthM; }

	// ---------------------------------------------------------------- Builds (replicated)

	TArray<FPuffersMapBuild> GetBuilds() const;
	// Seconds since the build appeared or changed on this client (for the ripple animation).
	double GetBuildAge(FName BuildId) const;

	UPROPERTY(BlueprintAssignable, Category = "Puffers|Map")
	FPuffersMapBuildChanged OnBuildChanged;

	// Called by APuffersMapReplicator on every client (and on the server for its own changes).
	void NotifyBuildChanged(const FPuffersMapBuild& Build, bool bRemoved);

	// ---------------------------------------------------------------- Screens

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void OpenMap();

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void CloseMap();

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void ToggleMap();

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	bool IsMapOpen() const { return bMapOpen; }

	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void SetMinimapVisible(bool bVisible);

	// 3D view: the real world from a camera rig, map drawn on top. Falls back to 2D on weak GPUs.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	bool SetViewMode(EPuffersMapViewMode Mode);

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	EPuffersMapViewMode GetViewMode() const { return ViewMode; }

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	bool Is3DAvailable(FString& OutWhyNot) const;

	APuffersMapCameraRig* GetCameraRig() const { return CameraRig; }

	UPROPERTY(BlueprintAssignable, Category = "Puffers|Map")
	FPuffersMapOpenChanged OnMapOpenChanged;

	APlayerController* GetLocalPlayerController() const;

private:
	bool bInitialized = false;
	FPuffersMapConfig Config;
	FPuffersMapDataSet Data;
	int32 DataVersion = 0;

	TArray<FPuffersMapTileRef> SatelliteTiles;
	int32 SatelliteMaxLevel = 0;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UTexture2D>> TileTextures;
	TMap<int32, double> TileLastUse;
	TArray<int32> TileQueue;

	TArray<FPuffersMapMarker> Markers;
	bool bHasWaypoint = false;
	FVector2D Waypoint = FVector2D::ZeroVector;
	TArray<FVector2D> Route;
	double RouteLengthM = 0.0;
	double NextRouteTime = 0.0;
	FVector2D RouteFrom = FVector2D::ZeroVector;

	TMap<FName, double> BuildChangedAt;

	bool bMapOpen = false;
	bool bMinimapVisible = true;
	EPuffersMapViewMode ViewMode = EPuffersMapViewMode::Map2D;
	TSharedPtr<SPuffersMapView> FullView;
	TSharedPtr<SWidget> FullViewRoot;
	TSharedPtr<SPuffersMapView> MiniView;
	TSharedPtr<SWidget> MiniViewRoot;
	TWeakObjectPtr<UGameViewportClient> ViewportOwner;
	UPROPERTY(Transient)
	TObjectPtr<APuffersMapCameraRig> CameraRig;
	double SlowFrameSince = 0.0;

	void LoadConfig();
	void LoadSatelliteManifest();
	void EnsureMinimap();
	void RemoveWidgets();
	void UpdateRoute(bool bForce);
	void PumpTileLoads();
	void Spawn3DRig();
	void Destroy3DRig();
	APuffersMapReplicator* FindReplicator() const;
};
