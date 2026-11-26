#include "Missions/Steps/PuffersStepAIController.h"
#include "AI/PuffersNPCCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

APuffersStepAIController::APuffersStepAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
}

bool APuffersStepAIController::IsPaused() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < PauseUntil;
}

void APuffersStepAIController::SetSpeed(bool bInRun)
{
	bRun = bInRun;
	APuffersNPCCharacter* NPC = Cast<APuffersNPCCharacter>(GetPawn());
	if (!NPC)
	{
		return;
	}
	float Speed = bRun ? NPC->RunSpeed : NPC->WalkSpeed;
	if (GetWorld() && GetWorld()->GetTimeSeconds() < SlowUntil)
	{
		Speed *= SlowFactor;
	}
	NPC->GetCharacterMovement()->MaxWalkSpeed = Speed;
}

void APuffersStepAIController::Issue(const FVector& Goal, float Acceptance)
{
	DirectGoal = Goal;
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Goal, Acceptance, false, true, true, true, nullptr, true);
	bDirect = Result == EPathFollowingRequestResult::Failed;
}

void APuffersStepAIController::IssueActor(AActor* Goal, float Acceptance)
{
	if (!Goal)
	{
		return;
	}
	DirectGoal = Goal->GetActorLocation();
	const EPathFollowingRequestResult::Type Result = MoveToActor(Goal, Acceptance, false, true, true, nullptr, true);
	bDirect = Result == EPathFollowingRequestResult::Failed;
}

void APuffersStepAIController::SteerToward(const FVector& Goal)
{
	APawn* Self = GetPawn();
	if (!Self)
	{
		return;
	}
	const FVector To = Goal - Self->GetActorLocation();
	if (To.Size2D() > 40.f)
	{
		Self->AddMovementInput(To.GetSafeNormal2D(), 1.f);
	}
}

void APuffersStepAIController::HoldPosition(AActor* FaceActor)
{
	Mode = EPuffersStepAIMode::Hold;
	StopMovement();
	bDirect = false;
	FocusTarget = FaceActor;
	if (FaceActor)
	{
		SetFocus(FaceActor);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void APuffersStepAIController::GoTo(const FVector& Point, bool bInRun)
{
	Points = { Point };
	PointIndex = 0;
	bRouteFinished = false;
	Mode = EPuffersStepAIMode::Goto;
	ClearFocus(EAIFocusPriority::Gameplay);
	SetSpeed(bInRun);
	Issue(Point, 60.f);
}

void APuffersStepAIController::SetPatrol(const TArray<FVector>& InPoints, bool bInRun)
{
	Points = InPoints;
	PointIndex = 0;
	bRouteFinished = false;
	Mode = EPuffersStepAIMode::Patrol;
	ClearFocus(EAIFocusPriority::Gameplay);
	SetSpeed(bInRun);
	if (Points.Num() > 0)
	{
		Issue(Points[0], 60.f);
	}
}

void APuffersStepAIController::SetRoute(const TArray<FVector>& InPoints, bool bInRun)
{
	SetPatrol(InPoints, bInRun);
	Mode = EPuffersStepAIMode::Route;
}

void APuffersStepAIController::ChaseActor(AActor* Target)
{
	Mode = EPuffersStepAIMode::Chase;
	FocusTarget = Target;
	SetFocus(Target);
	SetSpeed(true);
	IssueActor(Target, 30.f);
	RepathTimer = 0.f;
}

void APuffersStepAIController::FollowActor(AActor* Leader, float Distance)
{
	Mode = EPuffersStepAIMode::Follow;
	FocusTarget = Leader;
	FollowDistance = Distance;
	ClearFocus(EAIFocusPriority::Gameplay);
	SetSpeed(false);
	IssueActor(Leader, Distance);
	RepathTimer = 0.f;
}

void APuffersStepAIController::FleeAlong(const TArray<FVector>& InPoints)
{
	SetRoute(InPoints, true);
	Mode = EPuffersStepAIMode::Flee;
}

void APuffersStepAIController::Pause(float Seconds, AActor* LookAt)
{
	PauseUntil = GetWorld()->GetTimeSeconds() + Seconds;
	StopMovement();
	if (LookAt)
	{
		SetFocus(LookAt);
	}
}

void APuffersStepAIController::Slow(float Seconds, float Factor)
{
	SlowUntil = GetWorld()->GetTimeSeconds() + Seconds;
	SlowFactor = Factor;
	SetSpeed(bRun);
}

void APuffersStepAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* Self = GetPawn();
	if (!Self)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < PauseUntil)
	{
		return;
	}
	if (PauseUntil > 0.f)
	{
		// Pause just ended: pick the walk back up.
		PauseUntil = 0.f;
		if (Mode != EPuffersStepAIMode::Chase)
		{
			ClearFocus(EAIFocusPriority::Gameplay);
		}
		if (Points.IsValidIndex(PointIndex) && (Mode == EPuffersStepAIMode::Patrol || Mode == EPuffersStepAIMode::Route
			|| Mode == EPuffersStepAIMode::Flee || Mode == EPuffersStepAIMode::Goto))
		{
			Issue(Points[PointIndex], 60.f);
		}
	}
	if (SlowUntil > 0.f && Now >= SlowUntil)
	{
		SlowUntil = 0.f;
		SetSpeed(bRun);
	}

	RepathTimer -= DeltaSeconds;
	const FVector Here = Self->GetActorLocation();

	switch (Mode)
	{
	case EPuffersStepAIMode::Hold:
		return;

	case EPuffersStepAIMode::Chase:
		if (AActor* Target = FocusTarget.Get())
		{
			if (RepathTimer <= 0.f)
			{
				IssueActor(Target, 30.f);
				RepathTimer = 0.4f;
			}
			if (bDirect)
			{
				SteerToward(Target->GetActorLocation());
			}
		}
		return;

	case EPuffersStepAIMode::Follow:
		if (AActor* Leader = FocusTarget.Get())
		{
			const float Gap = FVector::Dist2D(Leader->GetActorLocation(), Here);
			SetSpeed(Gap > FollowDistance * 2.5f);
			if (RepathTimer <= 0.f)
			{
				IssueActor(Leader, FollowDistance);
				RepathTimer = 0.5f;
			}
			if (bDirect && Gap > FollowDistance)
			{
				SteerToward(Leader->GetActorLocation());
			}
		}
		return;

	case EPuffersStepAIMode::Goto:
	case EPuffersStepAIMode::Patrol:
	case EPuffersStepAIMode::Route:
	case EPuffersStepAIMode::Flee:
	{
		if (!Points.IsValidIndex(PointIndex))
		{
			return;
		}
		const FVector Goal = Points[PointIndex];
		if (FVector::Dist2D(Goal, Here) < 120.f)
		{
			if (Mode == EPuffersStepAIMode::Patrol)
			{
				PointIndex = (PointIndex + 1) % Points.Num();
			}
			else if (PointIndex + 1 < Points.Num())
			{
				++PointIndex;
			}
			else
			{
				bRouteFinished = true;
				Mode = EPuffersStepAIMode::Hold;
				StopMovement();
				return;
			}
			Issue(Points[PointIndex], 60.f);
			return;
		}
		if (bDirect)
		{
			SteerToward(Goal);
		}
		else if (RepathTimer <= 0.f && GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			Issue(Goal, 60.f);
			RepathTimer = 1.f;
		}
		return;
	}
	default:
		return;
	}
}
