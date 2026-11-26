// Brain for NPCs that a mission step spawns (fleeing targets, pursuers, guards, crowds, escorts).
// Steps drive it directly. It paths on the navmesh when there is one and steers straight at the goal when
// there isn't, so steps also play on graybox levels without built navigation.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "PuffersStepAIController.generated.h"

UENUM()
enum class EPuffersStepAIMode : uint8
{
	Hold,      // stand still (optionally facing FaceActor)
	Goto,      // walk to one point, then hold
	Patrol,    // loop through points
	Route,     // walk through points once, then hold
	Chase,     // run at a target actor
	Follow,    // stay near a leader
	Flee       // run away from a threat through points
};

UCLASS()
class PUFFERSCLUB_API APuffersStepAIController : public AAIController
{
	GENERATED_BODY()

public:
	APuffersStepAIController();

	virtual void Tick(float DeltaSeconds) override;

	void HoldPosition(AActor* FaceActor = nullptr);
	void GoTo(const FVector& Point, bool bRun);
	void SetPatrol(const TArray<FVector>& Points, bool bRun);
	void SetRoute(const TArray<FVector>& Points, bool bRun);
	void ChaseActor(AActor* Target);
	void FollowActor(AActor* Leader, float Distance);
	void FleeAlong(const TArray<FVector>& Points);

	// Stops and looks around for a moment (used by tailed targets and suspicious guards).
	void Pause(float Seconds, AActor* LookAt = nullptr);

	// Moves slowly for a while (staggered, persuaded, confused by an ability).
	void Slow(float Seconds, float Factor = 0.3f);

	EPuffersStepAIMode GetMode() const { return Mode; }
	bool IsPaused() const;
	bool HasFinishedRoute() const { return bRouteFinished; }
	int32 GetRouteIndex() const { return PointIndex; }
	const TArray<FVector>& GetPoints() const { return Points; }

private:
	void Issue(const FVector& Goal, float Acceptance);
	void IssueActor(AActor* Goal, float Acceptance);
	void SetSpeed(bool bRun);
	void SteerToward(const FVector& Goal);

	EPuffersStepAIMode Mode = EPuffersStepAIMode::Hold;
	TArray<FVector> Points;
	int32 PointIndex = 0;
	bool bRun = false;
	bool bRouteFinished = false;
	bool bDirect = false;          // no navmesh path: steer straight at the goal
	FVector DirectGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> FocusTarget;
	float FollowDistance = 250.f;
	float RepathTimer = 0.f;
	float PauseUntil = 0.f;
	float SlowUntil = 0.f;
	float SlowFactor = 1.f;
};
