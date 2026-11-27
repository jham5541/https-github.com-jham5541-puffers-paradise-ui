// Records player builds and shares them with every player.
//
//   client career/build system ──ServerReportBuilds──▶ server UPuffersMapBuildService ──▶ APuffersMapReplicator ──▶ every client's map
//                                                              │
//                                                              └──▶ backend /v1/map/builds (Postgres) ◀── other servers poll it
//
// Online (dedicated or listen server with online.backendUrl / PUFFERS_BACKEND_URL set) builds persist in the backend and
// builds made on other servers appear within a few seconds. Offline they persist in Saved/PuffersMap/Builds.json.
//
// Hooks:
//   * Career plots: UPuffersMapNetComponent listens for "PlotBuildingChanged" and resyncs the player's plots.
//   * Any build system (APuffersBuildZone, Blueprints): call RecordBuildFromActor on the server, or
//     UPuffersMapNetComponent::ServerReportBuilds from the owning client.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "Map/PuffersMapTypes.h"
#include "PuffersMapBuildService.generated.h"

class APuffersMapReplicator;
class AGameModeBase;
class UPuffersMapSubsystem;
class FJsonObject;

UCLASS()
class PUFFERSCLUB_API UPuffersMapBuildService : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ---------------------------------------------------------------- Server API

	// Adds or updates a build on everyone's map. Server only.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	bool RecordBuild(FPuffersMapBuild Build, FString& OutWhy);

	// Footprint comes from the actor's bounds (rotated with it). Server only.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	bool RecordBuildFromActor(AActor* Building, FName BuildId, FName PlotId, FName OwnerId, const FString& OwnerName, FName Use,
		EPuffersMapBuildState State, const FString& Label, FString& OutWhy);

	// Marks a build demolished (kept so other servers learn about it, hidden on the map). Server only.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	bool RemoveBuild(FName BuildId);

	UFUNCTION(BlueprintPure, Category = "Puffers|Map")
	bool HasAuthority() const;

	APuffersMapReplicator* GetReplicator() const { return Replicator; }

	// ---------------------------------------------------------------- Footprints (any net mode)

	static TArray<FVector2D> FootprintFromActor(const UPuffersMapSubsystem* Map, const AActor* Actor);
	// A lot actor tagged (or named) LotId, e.g. an APuffersBuildZone in L_Outskirts.
	bool FindLotFootprint(FName LotId, TArray<FVector2D>& Out) const;
	// The lot actor if it exists; otherwise a stable spot inside the county's lot zones from WorldLayout.json
	// (hashed from the plot id) so every client and server agree on where the plot is.
	TArray<FVector2D> ResolvePlotFootprint(FName PlotId, FName LotId, FName CountyId, int32 Cells, int32 BuildingIndex, int32 BuildingCount) const;

	// ---------------------------------------------------------------- JSON (backend + local save)

	static TSharedRef<FJsonObject> BuildToJson(const FPuffersMapBuild& Build);
	static bool BuildFromJson(const TSharedPtr<FJsonObject>& Obj, FPuffersMapBuild& Out);

	// Basic sanity checks shared by RecordBuild and the RPC (size, point count, text length).
	static bool Sanitize(FPuffersMapBuild& Build, FString& OutWhy);

private:
	UPROPERTY(Transient)
	TObjectPtr<APuffersMapReplicator> Replicator;

	FDelegateHandle PostLoginHandle;
	mutable TMap<FName, TArray<FVector2D>> LotCache;

	int64 Cursor = 0;
	double NextPoll = 0.0;
	bool bPollInFlight = false;
	TMap<FName, FPuffersMapBuild> PendingPush;
	double NextPush = 0.0;
	bool bPushInFlight = false;
	bool bLocalDirty = false;
	double NextLocalSave = 0.0;

	UPuffersMapSubsystem* GetMap() const;
	bool UsesBackend() const;
	FString BackendUrl() const;
	void AttachNetComponent(APlayerController* PC);
	void OnPostLogin(AGameModeBase* GameMode, APlayerController* PC);
	void PollBackend();
	void PushBackend();
	void LoadLocal();
	void SaveLocal();
};

// Added to every PlayerController by the server. Carries a player's build reports to the server and syncs their career plots.
UCLASS(ClassGroup = (Puffers), meta = (BlueprintSpawnableComponent))
class PUFFERSCLUB_API UPuffersMapNetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPuffersMapNetComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Owning client -> server. The server fills in the owner from the player state and rejects edits to other players' builds.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Puffers|Map")
	void ServerReportBuilds(const TArray<FPuffersMapBuild>& Builds);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Puffers|Map")
	void ServerRemoveBuild(FName BuildId);

	// Client: sends every building on this player's career plots.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Map")
	void ReportCareerPlots();

private:
	UFUNCTION()
	void HandleCareerEvent(FName EventName, FName Context);

	FTimerHandle SyncTimer;
	TSet<FName> LastReported;
	bool bBound = false;

	bool IsOwningClient() const;
	void BindCareer();
};
