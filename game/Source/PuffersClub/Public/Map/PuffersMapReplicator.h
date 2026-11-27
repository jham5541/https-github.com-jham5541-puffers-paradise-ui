// Server-authoritative list of player builds, replicated to every client with a fast array (only changes go over the wire).
// One per world, spawned by UPuffersMapBuildService on the server. Clients read it through UPuffersMapSubsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Map/PuffersMapTypes.h"
#include "PuffersMapReplicator.generated.h"

UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersMapReplicator : public AActor
{
	GENERATED_BODY()

public:
	APuffersMapReplicator();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;

	const TArray<FPuffersMapBuild>& GetBuilds() const { return Builds.Items; }
	const FPuffersMapBuild* FindBuild(FName BuildId) const;

	// Server only. Returns true if anything changed. Older data (lower Seq) never overwrites newer.
	bool Upsert(const FPuffersMapBuild& Build);
	bool Remove(FName BuildId);
	void ReplaceAll(const TArray<FPuffersMapBuild>& InBuilds);

	// Fast-array callbacks on clients.
	void HandleReplicated(const FPuffersMapBuild& Build, bool bRemoved);

	static bool SameContent(const FPuffersMapBuild& A, const FPuffersMapBuild& B);

private:
	UPROPERTY(Replicated)
	FPuffersMapBuildArray Builds;

	void NotifyLocal(const FPuffersMapBuild& Build, bool bRemoved);
};
