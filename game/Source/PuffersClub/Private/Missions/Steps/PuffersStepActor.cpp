#include "Missions/Steps/PuffersStepActor.h"
#include "Missions/Steps/PuffersStepAIController.h"
#include "Missions/Steps/PuffersStepRules.h"
#include "Missions/Steps/PuffersStepSubsystem.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Player/PuffersPlayerCharacter.h"
#include "Systems/PuffersHeatSubsystem.h"
#include "Vehicles/PuffersVehicle.h"
#include "World/PuffersInteractableComponent.h"
#include "World/PuffersWorldActors.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"

// Marker

APuffersStepMarker::APuffersStepMarker()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetMobility(EComponentMobility::Movable);
	RootComponent = Mesh;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetAbsolute(false, false, true);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(32.f);
}

void APuffersStepMarker::Setup(const FString& Shape, const FVector& Scale, const FLinearColor& Color, const FString& Text)
{
	const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *Shape, *Shape);
	if (UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *Path))
	{
		Mesh->SetStaticMesh(StaticMesh);
	}
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		Mesh->SetMaterial(0, Material);
	}
	Mesh->SetWorldScale3D(Scale);
	Label->SetWorldScale3D(FVector(1.f));
	SetColor(Color);
	SetText(Text);
}

void APuffersStepMarker::SetColor(const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Label->SetTextRenderColor(Color.ToFColor(true));
}

void APuffersStepMarker::SetText(const FString& Text)
{
	Label->SetText(FText::FromString(Text));
	Label->SetVisibility(!Text.IsEmpty());
}

void APuffersStepMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Keep the label facing the camera.
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
			Label->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
		}
	}
}

// Runner base

APuffersStepActor::APuffersStepActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetCanBeDamaged(false);
}

void APuffersStepActor::Init(UPuffersStepSubsystem* InOwner, FName InMissionId, const FPuffersObjectiveDefinition& InObjective,
	const FPuffersStepParams& InParams, int32 InGeneration, AActor* InAnchorActor, const FVector& InAnchor)
{
	Owner = InOwner;
	MissionId = InMissionId;
	Objective = InObjective;
	Params = InParams;
	Generation = InGeneration;
	AnchorActor = InAnchorActor;
	Anchor = InAnchor;
	State = EPuffersStepState::Pending;
	HijackAnchor();
	OnPrepare();
}

FString APuffersStepActor::GetGameplayLabel() const
{
	return Params.Gameplay.ToString().ToUpper();
}

void APuffersStepActor::HijackAnchor()
{
	AActor* Actor = AnchorActor.Get();
	if (!Actor)
	{
		return;
	}
	if (APuffersObjectiveVolume* Volume = Cast<APuffersObjectiveVolume>(Actor))
	{
		if (Volume->ObjectiveId == Objective.Id)
		{
			HijackedVolume = Volume;
			Volume->ObjectiveId = NAME_None;
		}
		return;
	}
	if (APuffersNPCCharacter* Talker = Cast<APuffersNPCCharacter>(Actor))
	{
		if (Talker->TalkObjectiveIds.Remove(Objective.Id) > 0)
		{
			HijackedTalker = Talker;
			Talker->Interactable->OnInteracted.AddDynamic(this, &APuffersStepActor::OnAnchorUsed);
			return;
		}
	}
	HijackObjectiveProp(Objective.Id);
	if (Hijacked.Num() == 0)
	{
		// The anchor has an interactable that isn't tied to the objective (a spawned NPC): just listen to it.
		if (UPuffersInteractableComponent* Component = Actor->FindComponentByClass<UPuffersInteractableComponent>())
		{
			Hijacked.Add({ Component, NAME_None });
			Component->OnInteracted.AddUniqueDynamic(this, &APuffersStepActor::OnAnchorUsed);
		}
	}
}

void APuffersStepActor::HijackObjectiveProp(FName OtherObjectiveId)
{
	for (const TWeakObjectPtr<UPuffersInteractableComponent>& Entry : UPuffersInteractableComponent::GetRegistered())
	{
		UPuffersInteractableComponent* Component = Entry.Get();
		if (Component && Component->GetWorld() == GetWorld() && Component->ObjectiveId == OtherObjectiveId)
		{
			Hijacked.Add({ Component, OtherObjectiveId });
			Component->ObjectiveId = NAME_None;
			Component->OnInteracted.AddUniqueDynamic(this, &APuffersStepActor::OnAnchorUsed);
			if (!AnchorActor.IsValid())
			{
				AnchorActor = Component->GetOwner();
			}
		}
	}
}

void APuffersStepActor::ListenTo(UPuffersInteractableComponent* Component)
{
	if (Component)
	{
		Hijacked.Add({ Component, NAME_None });
		Component->OnInteracted.AddUniqueDynamic(this, &APuffersStepActor::OnAnchorUsed);
	}
}

void APuffersStepActor::Leave()
{
	if (State != EPuffersStepState::Running)
	{
		return;
	}
	OnEnd();
	ClearTemporary();
	LockMovement(false);
	State = EPuffersStepState::Cooldown;
	CooldownLeft = 1.f;
}

void APuffersStepActor::ReleaseAnchor()
{
	for (const FHijack& Entry : Hijacked)
	{
		if (UPuffersInteractableComponent* Component = Entry.Component.Get())
		{
			Component->OnInteracted.RemoveDynamic(this, &APuffersStepActor::OnAnchorUsed);
			if (!Entry.SavedObjective.IsNone())
			{
				Component->ObjectiveId = Entry.SavedObjective;
			}
			Component->ResetUse();
		}
	}
	Hijacked.Reset();
	if (APuffersObjectiveVolume* Volume = HijackedVolume.Get())
	{
		Volume->ObjectiveId = Objective.Id;
	}
	HijackedVolume.Reset();
	if (APuffersNPCCharacter* Talker = HijackedTalker.Get())
	{
		Talker->TalkObjectiveIds.AddUnique(Objective.Id);
		Talker->Interactable->OnInteracted.RemoveDynamic(this, &APuffersStepActor::OnAnchorUsed);
	}
	HijackedTalker.Reset();
}

void APuffersStepActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State == EPuffersStepState::Running)
	{
		OnEnd();
	}
	ClearTemporary();
	for (AActor* Actor : Persistent)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Persistent.Reset();
	LockMovement(false);
	ReleaseAnchor();
	if (UPuffersStepSubsystem* Subsystem = Owner.Get())
	{
		if (!Params.GrantAbility.IsNone())
		{
			Subsystem->RevokeTemporaryAbility(Params.GrantAbility);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void APuffersStepActor::OnAnchorUsed(AActor*)
{
	for (const FHijack& Entry : Hijacked)
	{
		if (UPuffersInteractableComponent* Component = Entry.Component.Get())
		{
			Component->ResetUse();
		}
	}
	if (State == EPuffersStepState::Pending)
	{
		bStartRequested = true;
	}
}

void APuffersStepActor::TryStart()
{
	UPuffersStepSubsystem* Subsystem = Owner.Get();
	if (Subsystem && !Subsystem->CanBeginChoice(this))
	{
		Toast(TEXT("You're already committed to the other option."));
		return;
	}
	Begin();
}

void APuffersStepActor::Begin()
{
	State = EPuffersStepState::Running;
	RunTime = 0.f;
	StatusLine.Reset();
	StartLocation = PlayerLocation();
	bStartRequested = false;

	UPuffersStepSubsystem* Subsystem = Owner.Get();
	if (Subsystem)
	{
		// "Haze: get insulin to the senior center" / "As Kush, hold the crowd" plays as that Puffer.
		const FName Prefix = FPuffersStepRules::ParseCharacterPrefix(Objective.Description, Subsystem->GetKnownCharacters());
		if (!Prefix.IsNone() && Params.Gameplay != TEXT("Switch"))
		{
			Subsystem->SwitchToCharacter(Prefix);
		}
		if (!Params.GrantAbility.IsNone())
		{
			Subsystem->GrantTemporaryAbility(Params.GrantAbility);
		}
	}
	if (Params.Heat > 0)
	{
		if (UPuffersHeatSubsystem* Heat = GetWorld()->GetSubsystem<UPuffersHeatSubsystem>())
		{
			Heat->RaiseHeat(Params.Heat, Objective.Id);
		}
	}
	if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Missions->BroadcastGameEvent(UPuffersStepSubsystem::EventStepStarted, Objective.Id);
	}
	if (State == EPuffersStepState::Running)
	{
		OnBegin();
	}
}

void APuffersStepActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UPuffersStepSubsystem* Subsystem = Owner.Get();
	if (!Subsystem || Subsystem->GetGeneration() != Generation)
	{
		return;
	}

	switch (State)
	{
	case EPuffersStepState::Pending:
	{
		const AActor* Actor = AnchorActor.Get();
		const bool bCanInteract = Hijacked.Num() > 0 || HijackedTalker.IsValid();
		FName Mode = Params.StartMode;
		if (Mode == TEXT("Interact") && !bCanInteract)
		{
			Mode = TEXT("Proximity");
		}
		if (Mode == TEXT("Immediate") || bStartRequested || (Mode == TEXT("Proximity") && PlayerDistance(Actor ? Actor->GetActorLocation() : Anchor) < 450.f))
		{
			TryStart();
			bStartRequested = false;
		}
		break;
	}
	case EPuffersStepState::Running:
		RunTime += DeltaSeconds;
		OnRunning(DeltaSeconds);
		break;
	case EPuffersStepState::Cooldown:
		CooldownLeft -= DeltaSeconds;
		if (CooldownLeft <= 0.f)
		{
			State = EPuffersStepState::Pending;
			bStartRequested = false;
		}
		break;
	default:
		break;
	}
}

void APuffersStepActor::BuildOverlay(FPuffersStepOverlay& Out) const
{
	if (State == EPuffersStepState::Running)
	{
		Out.Header = FString::Printf(TEXT("%s  -  %s"), *GetGameplayLabel(), *Objective.Description);
		if (GetRequired() > 1)
		{
			Out.Header += FString::Printf(TEXT("  (%d/%d)"), GetProgress(), GetRequired());
		}
		FillOverlay(Out);
		if (!StatusLine.IsEmpty())
		{
			Out.Lines.Add(StatusLine);
		}
		return;
	}
	if (State == EPuffersStepState::Pending)
	{
		const AActor* Actor = AnchorActor.Get();
		FPuffersStepMarker Marker;
		Marker.Location = Actor ? Actor->GetActorLocation() : Anchor;
		const bool bCanInteract = Hijacked.Num() > 0 || HijackedTalker.IsValid();
		const FString How = Params.StartMode == TEXT("Interact") && bCanInteract ? TEXT("[E] ") : TEXT("");
		Marker.Text = FString::Printf(TEXT("%s%s: %s"), *How, *GetGameplayLabel(), *Objective.Description.Left(56));
		Marker.Color = Objective.ChoiceGroup.IsNone() ? FLinearColor(1.f, 0.77f, 0.25f) : FLinearColor(0.7f, 0.5f, 1.f);
		Out.Markers.Add(Marker);
	}
}

void APuffersStepActor::Succeed(const FString& Message, int32 Amount)
{
	if (State != EPuffersStepState::Running)
	{
		return;
	}
	OnEnd();
	ClearTemporary();
	LockMovement(false);
	State = EPuffersStepState::Done;
	if (!Message.IsEmpty())
	{
		Toast(Message);
	}
	UPuffersMissionSubsystem* Missions = GetMissions();
	if (!Missions)
	{
		return;
	}
	const int32 Gen = Generation;
	Missions->BroadcastGameEvent(UPuffersStepSubsystem::EventStepSucceeded, Objective.Id);
	Missions->ReportObjectiveProgress(Objective.Id, Amount);

	// More units needed (3 photos, 5 drops...): go again. The subsystem drops this runner if the objective is done.
	const UPuffersStepSubsystem* Subsystem = Owner.Get();
	if (Subsystem && Subsystem->GetGeneration() == Gen && Missions->GetObjectiveProgress(Objective.Id) < Objective.RequiredCount)
	{
		State = EPuffersStepState::Cooldown;
		CooldownLeft = 0.6f;
		if (Params.StartMode != TEXT("Immediate"))
		{
			// Keep the momentum: start the next unit straight away.
			bStartRequested = true;
			State = EPuffersStepState::Pending;
		}
	}
}

bool APuffersStepActor::ReportFor(FName OtherObjectiveId, int32 Amount)
{
	if (State != EPuffersStepState::Running)
	{
		return false;
	}
	OnEnd();
	ClearTemporary();
	LockMovement(false);
	State = EPuffersStepState::Done;
	UPuffersMissionSubsystem* Missions = GetMissions();
	return Missions && Missions->ReportObjectiveProgress(OtherObjectiveId, Amount);
}

void APuffersStepActor::Fail(const FString& Reason, bool bReturnPlayer)
{
	if (State != EPuffersStepState::Running)
	{
		return;
	}
	OnEnd();
	ClearTemporary();
	LockMovement(false);
	State = EPuffersStepState::Cooldown;
	CooldownLeft = 3.f;
	Toast(FString::Printf(TEXT("%s Try again."), *Reason), true);
	if (bReturnPlayer)
	{
		ReturnPlayerToStart();
	}
	for (const FHijack& Entry : Hijacked)
	{
		if (UPuffersInteractableComponent* Component = Entry.Component.Get())
		{
			Component->ResetUse();
		}
	}
	if (UPuffersMissionSubsystem* Missions = GetMissions())
	{
		Missions->BroadcastGameEvent(UPuffersStepSubsystem::EventStepFailed, Objective.Id);
	}
}

bool APuffersStepActor::BroadcastAndCheck(FName EventName, FName Context)
{
	UPuffersMissionSubsystem* Missions = GetMissions();
	if (!Missions)
	{
		return true;
	}
	const FName Mission = Missions->GetActiveMissionId();
	const int32 Stage = Missions->GetActiveStage();
	const int32 Progress = Missions->GetObjectiveProgress(Objective.Id);
	Missions->BroadcastGameEvent(EventName, Context);
	// The mission failed (and restarted from a checkpoint) or was arrested away: this attempt is over.
	return Missions->GetActiveMissionId() == Mission && Missions->GetActiveStage() == Stage
		&& Missions->GetObjectiveProgress(Objective.Id) == Progress && !IsActorBeingDestroyed();
}

void APuffersStepActor::Toast(const FString& Text, bool bAlert) const
{
	if (UPuffersStepSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->ShowToast(Text, bAlert);
	}
}

UPuffersMissionSubsystem* APuffersStepActor::GetMissions() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPuffersMissionSubsystem>() : nullptr;
}

APlayerController* APuffersStepActor::GetPC() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}

APawn* APuffersStepActor::GetPlayerPawn() const
{
	const APlayerController* PC = GetPC();
	return PC ? PC->GetPawn() : nullptr;
}

APuffersPlayerCharacter* APuffersStepActor::GetPlayerCharacter() const
{
	return Cast<APuffersPlayerCharacter>(GetPlayerPawn());
}

APuffersVehicle* APuffersStepActor::GetPlayerVehicle() const
{
	return Cast<APuffersVehicle>(GetPlayerPawn());
}

bool APuffersStepActor::IsPlayerCrouched() const
{
	const APuffersPlayerCharacter* Player = GetPlayerCharacter();
	return Player && Player->bIsCrouched;
}

bool APuffersStepActor::IsPlayerSprinting() const
{
	const APuffersPlayerCharacter* Player = GetPlayerCharacter();
	return Player && Player->GetVelocity().Size2D() > Player->WalkSpeed + 60.f;
}

FVector APuffersStepActor::PlayerLocation() const
{
	const APawn* Pawn = GetPlayerPawn();
	return Pawn ? Pawn->GetActorLocation() : Anchor;
}

float APuffersStepActor::PlayerDistance(const FVector& Location) const
{
	return FVector::Dist2D(PlayerLocation(), Location);
}

bool APuffersStepActor::KeyPressed(const FKey& Key) const
{
	const APlayerController* PC = GetPC();
	return PC && PC->WasInputKeyJustPressed(Key);
}

bool APuffersStepActor::KeyDown(const FKey& Key) const
{
	const APlayerController* PC = GetPC();
	return PC && PC->IsInputKeyDown(Key);
}

bool APuffersStepActor::InteractPressed() const
{
	return KeyPressed(EKeys::E) || KeyPressed(EKeys::Gamepad_FaceButton_Left);
}

bool APuffersStepActor::InteractDown() const
{
	return KeyDown(EKeys::E) || KeyDown(EKeys::Gamepad_FaceButton_Left);
}

bool APuffersStepActor::FirePressed() const
{
	return KeyPressed(EKeys::LeftMouseButton) || KeyPressed(EKeys::Gamepad_LeftTrigger);
}

bool APuffersStepActor::FireDown() const
{
	return KeyDown(EKeys::LeftMouseButton) || KeyDown(EKeys::Gamepad_LeftTrigger);
}

bool APuffersStepActor::AimDown() const
{
	return KeyDown(EKeys::RightMouseButton) || KeyDown(EKeys::Gamepad_LeftShoulder);
}

int32 APuffersStepActor::OptionPressed() const
{
	if (KeyPressed(EKeys::One) || KeyPressed(EKeys::Gamepad_DPad_Up)) return 1;
	if (KeyPressed(EKeys::Two) || KeyPressed(EKeys::Gamepad_DPad_Right)) return 2;
	if (KeyPressed(EKeys::Three) || KeyPressed(EKeys::Gamepad_DPad_Down)) return 3;
	if (KeyPressed(EKeys::Four) || KeyPressed(EKeys::Gamepad_DPad_Left)) return 4;
	return 0;
}

bool APuffersStepActor::HasAbility(FName Ability) const
{
	const UPuffersStepSubsystem* Subsystem = Owner.Get();
	return Subsystem && Subsystem->HasAbility(Ability);
}

bool APuffersStepActor::IsAbilityActive(FName Ability) const
{
	const UPuffersStepSubsystem* Subsystem = Owner.Get();
	return Subsystem && Subsystem->IsAbilityActive(Ability);
}

float APuffersStepActor::GetDifficulty() const
{
	return FMath::Clamp(Params.Difficulty, 0.f, 1.f);
}

int32 APuffersStepActor::GetProgress() const
{
	const UPuffersMissionSubsystem* Missions = GetMissions();
	return Missions ? Missions->GetObjectiveProgress(Objective.Id) : 0;
}

FVector APuffersStepActor::GroundPoint(const FVector& Near) const
{
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PuffersStepGround), false);
	if (const APawn* Pawn = GetPlayerPawn())
	{
		Query.AddIgnoredActor(Pawn);
	}
	const FVector Start = Near + FVector(0.f, 0.f, 3000.f);
	const FVector End = Near - FVector(0.f, 0.f, 6000.f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query))
	{
		return Hit.ImpactPoint;
	}
	return Near;
}

FVector APuffersStepActor::RandomPointAround(const FVector& Center, float MinRadius, float MaxRadius, int32 Salt) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FRandomStream Random(FPuffersStepRules::StableIndex(Objective.Id, Salt + Generation * 7919 + FMath::FloorToInt(RunTime * 10.f), 1000003));
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		const float Radius = Random.FRandRange(MinRadius, MaxRadius);
		const FVector Candidate = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		if (Nav)
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(300.f, 300.f, 2000.f)))
			{
				return Projected.Location;
			}
		}
		else
		{
			return GroundPoint(Candidate);
		}
	}
	return GroundPoint(Center + FVector(MinRadius, 0.f, 0.f));
}

TArray<FVector> APuffersStepActor::MakeRoute(const FVector& From, int32 NumPoints, float StepMin, float StepMax, int32 Salt) const
{
	TArray<FVector> Route;
	FVector Last = From;
	FVector Heading = (From - PlayerLocation()).GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = FVector::ForwardVector;
	}
	FRandomStream Random(FPuffersStepRules::StableIndex(Objective.Id, Salt + Generation * 31, 1000003));
	for (int32 i = 0; i < NumPoints; ++i)
	{
		// Mostly away from the player, with turns, so a chase or tail actually goes somewhere.
		const float Turn = Random.FRandRange(-70.f, 70.f);
		Heading = Heading.RotateAngleAxis(Turn, FVector::UpVector);
		const FVector Candidate = Last + Heading * Random.FRandRange(StepMin, StepMax);
		FVector Point = Candidate;
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(400.f, 400.f, 2000.f)))
			{
				Point = Projected.Location;
			}
			else
			{
				Point = GroundPoint(Candidate);
			}
		}
		else
		{
			Point = GroundPoint(Candidate);
		}
		Route.Add(Point);
		Last = Point;
	}
	return Route;
}

bool APuffersStepActor::ClearLine(const FVector& From, const FVector& To, const AActor* IgnoreA, const AActor* IgnoreB) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PuffersStepSight), false);
	if (IgnoreA)
	{
		Query.AddIgnoredActor(IgnoreA);
	}
	if (IgnoreB)
	{
		Query.AddIgnoredActor(IgnoreB);
	}
	Query.AddIgnoredActor(this);
	// Only the level blocks sight; people and markers don't.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, From, To, Objects, Query);
}

bool APuffersStepActor::CameraAngleTo(const FVector& Location, float& OutAngle, float& OutDistance) const
{
	const APlayerController* PC = GetPC();
	if (!PC)
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector To = Location - ViewLocation;
	OutDistance = To.Size();
	if (OutDistance < 1.f)
	{
		OutAngle = 0.f;
		return true;
	}
	const float Cos = FVector::DotProduct(To / OutDistance, ViewRotation.Vector());
	OutAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f)));
	return true;
}

APuffersNPCCharacter* APuffersStepActor::SpawnNPC(const FVector& At, const FString& Name, EPuffersNPCRole Role, float WalkSpeed,
	float RunSpeed, bool bTalkable, bool bPersistent)
{
	UWorld* World = GetWorld();
	const FVector Ground = GroundPoint(At);
	const FRotator Facing(0.f, (PlayerLocation() - Ground).Rotation().Yaw, 0.f);
	const FTransform Transform(Facing, Ground + FVector(0.f, 0.f, 100.f));
	APuffersNPCCharacter* NPC = World->SpawnActorDeferred<APuffersNPCCharacter>(APuffersNPCCharacter::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!NPC)
	{
		return nullptr;
	}
	static int32 Serial = 0;
	NPC->AIControllerClass = APuffersStepAIController::StaticClass();
	NPC->NPCId = *FString::Printf(TEXT("Step_%s_%d"), *Objective.Id.ToString(), ++Serial);
	NPC->DisplayName = FText::FromString(Name);
	NPC->NPCRole = Role;
	NPC->WanderRadius = 0.f;
	NPC->WalkSpeed = WalkSpeed;
	NPC->RunSpeed = RunSpeed;
	NPC->PersuadeAbility = TEXT("GiftOfGab");
	NPC->FinishSpawning(Transform);
	if (!bTalkable)
	{
		NPC->Interactable->SetActive(false);
	}
	(bPersistent ? Persistent : Temporary).Add(NPC);
	return NPC;
}

APuffersStepMarker* APuffersStepActor::SpawnMarker(const FVector& At, const FString& Shape, const FVector& Scale, const FLinearColor& Color,
	const FString& Text, bool bPersistent)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APuffersStepMarker* Marker = GetWorld()->SpawnActor<APuffersStepMarker>(APuffersStepMarker::StaticClass(), GroundPoint(At),
		FRotator::ZeroRotator, SpawnParams);
	if (Marker)
	{
		Marker->Setup(Shape, Scale, Color, Text);
		(bPersistent ? Persistent : Temporary).Add(Marker);
	}
	return Marker;
}

APuffersStepAIController* APuffersStepActor::AI(APuffersNPCCharacter* NPC)
{
	return NPC ? Cast<APuffersStepAIController>(NPC->GetController()) : nullptr;
}

EPuffersNPCRole APuffersStepActor::RoleFromParams(EPuffersNPCRole Fallback) const
{
	if (Params.Role == TEXT("Police")) return EPuffersNPCRole::Police;
	if (Params.Role == TEXT("Security")) return EPuffersNPCRole::Security;
	if (Params.Role == TEXT("Enforcer")) return EPuffersNPCRole::Enforcer;
	if (Params.Role == TEXT("Inmate")) return EPuffersNPCRole::Inmate;
	if (Params.Role == TEXT("CorrectionalOfficer")) return EPuffersNPCRole::CorrectionalOfficer;
	if (Params.Role == TEXT("Pedestrian")) return EPuffersNPCRole::Pedestrian;
	return Fallback;
}

FString APuffersStepActor::SpeakerName() const
{
	const UPuffersStepSubsystem* Subsystem = Owner.Get();
	if (!Params.Target.IsNone())
	{
		return Subsystem ? Subsystem->DisplayNameFor(Params.Target) : Params.Target.ToString();
	}
	if (Subsystem)
	{
		const FName Named = FPuffersStepRules::FindNamedCharacter(Objective.Description, Subsystem->GetKnownCharacters());
		if (!Named.IsNone())
		{
			return Subsystem->DisplayNameFor(Named);
		}
	}
	return FString();
}

void APuffersStepActor::LockMovement(bool bLock)
{
	if (bLock == bMovementLocked)
	{
		return;
	}
	if (APlayerController* PC = GetPC())
	{
		PC->SetIgnoreMoveInput(bLock);
	}
	bMovementLocked = bLock;
}

void APuffersStepActor::ReturnPlayerToStart()
{
	if (APawn* Pawn = GetPlayerPawn())
	{
		Pawn->TeleportTo(StartLocation, Pawn->GetActorRotation(), false, true);
	}
}

void APuffersStepActor::ClearTemporary()
{
	for (AActor* Actor : Temporary)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Temporary.Reset();
}
