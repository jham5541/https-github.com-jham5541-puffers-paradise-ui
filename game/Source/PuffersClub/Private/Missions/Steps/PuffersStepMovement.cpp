// Movement steps: Reach / Route / EnterVehicle, Delivery, Chase, Escape, Tail / Escort, Stealth / Approach, LoseHeat.

#include "Missions/Steps/PuffersStepActor.h"
#include "Missions/Steps/PuffersStepAIController.h"
#include "Missions/Steps/PuffersStepRules.h"
#include "Missions/Steps/PuffersStepSubsystem.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Player/PuffersPlayerCharacter.h"
#include "Systems/PuffersHeatSubsystem.h"
#include "Vehicles/PuffersGarageSubsystem.h"
#include "Vehicles/PuffersVehicle.h"
#include "World/PuffersWorldActors.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const FLinearColor Gold(1.f, 0.77f, 0.25f);
	const FLinearColor Blue(0.4f, 0.75f, 1.f);
	const FLinearColor Red(1.f, 0.3f, 0.25f);
	const FLinearColor Green(0.45f, 0.85f, 0.5f);

	FString Clock(float Seconds)
	{
		const int32 S = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
	}

	FPuffersStepMarker MakeMarker(const FVector& Location, const FString& Text, const FLinearColor& Color, bool bDistance = true)
	{
		FPuffersStepMarker Marker;
		Marker.Location = Location;
		Marker.Text = Text;
		Marker.Color = Color;
		Marker.bShowDistance = bDistance;
		return Marker;
	}
}

// Travel ------------------------------------------------------------------------------------------------------

FString APuffersStepTravel::GetGameplayLabel() const
{
	if (Params.Gameplay == TEXT("EnterVehicle")) return TEXT("GET IN");
	if (Params.Gameplay == TEXT("Route")) return TEXT("DRIVE");
	return Params.bVehicle ? TEXT("DRIVE") : TEXT("GO");
}

void APuffersStepTravel::OnBegin()
{
	TimeLimit = Params.Seconds > 0.f ? Params.Seconds * (HasAbility(TEXT("RouteMastery")) ? 1.25f : 1.f) : 0.f;

	if (Params.Gameplay == TEXT("EnterVehicle"))
	{
		bool bNearbyVehicle = GetPlayerVehicle() != nullptr;
		for (TActorIterator<APuffersVehicle> It(GetWorld()); It && !bNearbyVehicle; ++It)
		{
			bNearbyVehicle = FVector::Dist(It->GetActorLocation(), Anchor) < 4000.f;
		}
		const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
		const UPuffersGarageSubsystem* Garage = GameInstance ? GameInstance->GetSubsystem<UPuffersGarageSubsystem>() : nullptr;
		if (!bNearbyVehicle && Garage)
		{
			for (const FPuffersVehicleDefinition& Def : Garage->GetDefinitions())
			{
				if (Def.Type == EPuffersVehicleType::Plane)
				{
					continue;
				}
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector At = GroundPoint(Anchor + FVector(300.f, 0.f, 0.f)) + FVector(0.f, 0.f, 150.f);
				if (APuffersVehicle* Vehicle = GetWorld()->SpawnActor<APuffersVehicle>(APuffersVehicle::StaticClass(), At, FRotator::ZeroRotator, SpawnParams))
				{
					// Left in the world on purpose: the player is about to drive it.
					Vehicle->Configure(Def, FGuid());
				}
				break;
			}
		}
		StatusLine = TEXT("Walk up to the vehicle and press E to get in.");
		return;
	}

	if (Params.Gameplay == TEXT("Route"))
	{
		Checkpoints = MakeRoute(Anchor, Params.Count > 0 ? Params.Count : 4, 1500.f, 2600.f, 11);
		NextCheckpoint = 0;
		if (Checkpoints.Num() > 0)
		{
			CheckpointMarker = SpawnMarker(Checkpoints[0], TEXT("Cylinder"), FVector(4.f, 4.f, 0.05f), Blue, TEXT("CHECKPOINT"));
		}
	}
}

void APuffersStepTravel::OnRunning(float DeltaSeconds)
{
	if (Params.Gameplay == TEXT("EnterVehicle"))
	{
		if (GetPlayerVehicle())
		{
			Succeed(TEXT("Rolling."));
		}
		return;
	}
	if (TimeLimit > 0.f && Elapsed() > TimeLimit)
	{
		Fail(TEXT("Out of time."));
		return;
	}
	const bool bNeedsVehicle = Params.bVehicle && !GetPlayerVehicle();
	StatusLine = bNeedsVehicle ? TEXT("You need wheels: press H to call your car, then E to get in.") : FString();

	if (Params.Gameplay == TEXT("Route"))
	{
		if (!Checkpoints.IsValidIndex(NextCheckpoint))
		{
			Succeed(TEXT("Route done."));
			return;
		}
		if (!bNeedsVehicle && PlayerDistance(Checkpoints[NextCheckpoint]) < 500.f)
		{
			++NextCheckpoint;
			if (APuffersStepMarker* Marker = CheckpointMarker.Get())
			{
				if (Checkpoints.IsValidIndex(NextCheckpoint))
				{
					Marker->SetActorLocation(GroundPoint(Checkpoints[NextCheckpoint]));
				}
				else
				{
					Marker->Destroy();
				}
			}
		}
		return;
	}

	const float Radius = Params.Radius > 0.f ? Params.Radius : (Params.bVehicle ? 700.f : 400.f);
	if (!bNeedsVehicle && PlayerDistance(Anchor) < Radius)
	{
		Succeed();
	}
}

void APuffersStepTravel::OnEnd()
{
	Checkpoints.Reset();
	CheckpointMarker.Reset();
}

void APuffersStepTravel::FillOverlay(FPuffersStepOverlay& Out) const
{
	if (TimeLimit > 0.f)
	{
		Out.Lines.Add(FString::Printf(TEXT("Time left %s"), *Clock(TimeLimit - Elapsed())));
	}
	if (Params.Gameplay == TEXT("Route"))
	{
		if (Checkpoints.IsValidIndex(NextCheckpoint))
		{
			Out.Markers.Add(MakeMarker(Checkpoints[NextCheckpoint], FString::Printf(TEXT("CHECKPOINT %d/%d"), NextCheckpoint + 1, Checkpoints.Num()), Blue));
		}
		return;
	}
	if (Params.Gameplay == TEXT("EnterVehicle"))
	{
		for (TActorIterator<APuffersVehicle> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), Anchor) < 4000.f)
			{
				Out.Markers.Add(MakeMarker(It->GetActorLocation(), TEXT("[E] GET IN"), Gold));
				break;
			}
		}
		return;
	}
	Out.Markers.Add(MakeMarker(Anchor, TEXT("DESTINATION"), Blue));
}

// Delivery ----------------------------------------------------------------------------------------------------

void APuffersStepDelivery::OnBegin()
{
	Damage = 0.f;
	TimeLimit = Params.Seconds > 0.f ? Params.Seconds * (HasAbility(TEXT("RouteMastery")) ? 1.25f : 1.f) : 0.f;
	// After the first drop of a multi-drop run you're already carrying the rest.
	bCarrying = GetProgress() > 0;
	if (bCarrying)
	{
		NextDrop();
	}
	else
	{
		Package = SpawnMarker(Anchor, TEXT("Cube"), FVector(0.4f, 0.4f, 0.3f), FLinearColor(0.6f, 0.4f, 0.2f), TEXT("PACKAGE"));
	}
}

void APuffersStepDelivery::NextDrop()
{
	const float Distance = Params.Distance > 0.f ? Params.Distance : 2600.f;
	DropPoint = RandomPointAround(Anchor, Distance * 0.6f, Distance, 23 + GetProgress());
	if (APuffersStepMarker* Old = DropMarker.Get())
	{
		Old->Destroy();
	}
	DropMarker = SpawnMarker(DropPoint, TEXT("Cylinder"), FVector(3.f, 3.f, 0.05f), Green, TEXT("DROP-OFF"));
}

void APuffersStepDelivery::OnRunning(float DeltaSeconds)
{
	if (TimeLimit > 0.f && Elapsed() > TimeLimit)
	{
		Fail(TEXT("Too late."));
		return;
	}
	if (!bCarrying)
	{
		StatusLine = TEXT("Pick up the package.");
		if (PlayerDistance(Anchor) < 260.f)
		{
			bCarrying = true;
			if (APuffersStepMarker* Box = Package.Get())
			{
				Box->Destroy();
			}
			NextDrop();
		}
		return;
	}

	if (Params.bFragile)
	{
		const APuffersPlayerCharacter* Player = GetPlayerCharacter();
		const bool bFalling = Player && Player->GetCharacterMovement()->IsFalling();
		const APuffersVehicle* Vehicle = GetPlayerVehicle();
		const bool bRough = IsPlayerSprinting() || bFalling || (Vehicle && FMath::Abs(Vehicle->GetSpeed()) > 1800.f);
		Damage += (bRough ? 0.35f : -0.05f) * DeltaSeconds * FMath::Lerp(0.8f, 1.4f, GetDifficulty());
		Damage = FMath::Max(0.f, Damage);
		StatusLine = bRough ? TEXT("Easy! It's fragile.") : TEXT("Careful with it.");
		if (Damage >= 1.f)
		{
			Fail(TEXT("It broke."));
			return;
		}
	}
	else
	{
		StatusLine = TEXT("Get it to the drop-off.");
	}
	if (PlayerDistance(DropPoint) < 320.f)
	{
		Succeed(GetRequired() > 1 ? TEXT("Dropped off. Next one.") : TEXT("Delivered."));
	}
}

void APuffersStepDelivery::OnEnd()
{
	DropMarker.Reset();
	Package.Reset();
}

void APuffersStepDelivery::FillOverlay(FPuffersStepOverlay& Out) const
{
	if (TimeLimit > 0.f)
	{
		Out.Lines.Add(FString::Printf(TEXT("Time left %s"), *Clock(TimeLimit - Elapsed())));
	}
	if (Params.bFragile && bCarrying)
	{
		Out.Bar2 = Damage;
		Out.Bar2Label = TEXT("Damage");
	}
	Out.Markers.Add(bCarrying ? MakeMarker(DropPoint, TEXT("DROP-OFF"), Green) : MakeMarker(Anchor, TEXT("PICK UP"), Gold));
}

// Chase -------------------------------------------------------------------------------------------------------

void APuffersStepChase::OnBegin()
{
	TimeLimit = Params.Seconds > 0.f ? Params.Seconds : 45.f;
	const float Speed = Params.Speed > 0.f ? Params.Speed : (Params.bVehicle ? 950.f : 500.f + 80.f * GetDifficulty());
	const FString Name = SpeakerName().IsEmpty() ? TEXT("Target") : SpeakerName();
	Runner = SpawnNPC(Anchor, Name, RoleFromParams(EPuffersNPCRole::Pedestrian), 220.f, Speed, false);
	if (APuffersStepAIController* Brain = AI(Runner.Get()))
	{
		Brain->FleeAlong(MakeRoute(Anchor, 8, 900.f, 1500.f, 3));
	}
	GaspTimer = 6.f;
	Panic = 0.f;
	StatusLine = Params.bVehicle ? TEXT("Get close (H calls your car) and stay on them.") : TEXT("Sprint (Shift) to close in, then tackle with F or E.");
}

void APuffersStepChase::OnRunning(float DeltaSeconds)
{
	APuffersNPCCharacter* Target = Runner.Get();
	if (!Target)
	{
		Fail(TEXT("Lost them."));
		return;
	}
	APuffersStepAIController* Brain = AI(Target);
	if (Brain && Brain->HasFinishedRoute())
	{
		Brain->FleeAlong(MakeRoute(Target->GetActorLocation(), 6, 900.f, 1500.f, 5 + FMath::FloorToInt(Elapsed())));
	}

	// Runners get winded: short stumbles give you a window.
	GaspTimer -= DeltaSeconds;
	if (GaspTimer <= 0.f && Brain)
	{
		Brain->Pause(FMath::Lerp(1.1f, 0.5f, GetDifficulty()));
		GaspTimer = FMath::Lerp(5.f, 8.f, GetDifficulty());
	}

	const float Distance = PlayerDistance(Target->GetActorLocation());
	const float Reach = GetPlayerVehicle() ? 380.f : 190.f;
	if (Distance < Reach && (InteractPressed() || KeyPressed(EKeys::F) || GetPlayerVehicle()))
	{
		Succeed(TEXT("Got them."));
		return;
	}
	if (Distance > 4500.f)
	{
		Panic += DeltaSeconds;
		if (Panic > 3.f)
		{
			Fail(TEXT("They got away."));
			return;
		}
	}
	else
	{
		Panic = 0.f;
	}
	if (Elapsed() > TimeLimit)
	{
		Fail(TEXT("They got away."));
	}
}

void APuffersStepChase::OnGameEvent(FName EventName, FName Context)
{
	if (State != EPuffersStepState::Running || EventName != TEXT("ShovedNPC"))
	{
		return;
	}
	const APuffersNPCCharacter* Target = Runner.Get();
	if (Target && Context == Target->NPCId)
	{
		Succeed(TEXT("Tackled."));
	}
	else if (Params.bPeaceful)
	{
		Fail(TEXT("You knocked over a bystander. People are filming."));
	}
}

void APuffersStepChase::OnAbilityUsed(FName Ability)
{
	if (Ability == TEXT("WrongTurn") || Ability == TEXT("Trendsetter"))
	{
		if (APuffersStepAIController* Brain = AI(Runner.Get()))
		{
			Brain->Pause(2.5f);
			StatusLine = Ability == TEXT("WrongTurn") ? TEXT("They took a wrong turn!") : TEXT("They stopped to look at your fit.");
		}
	}
}

void APuffersStepChase::OnEnd()
{
	Runner.Reset();
}

void APuffersStepChase::FillOverlay(FPuffersStepOverlay& Out) const
{
	if (const APuffersNPCCharacter* Target = Runner.Get())
	{
		const float Distance = PlayerDistance(Target->GetActorLocation());
		Out.Markers.Add(MakeMarker(Target->GetActorLocation(), Distance < 190.f ? TEXT("[F] TACKLE") : TEXT("TARGET"), Red));
		Out.Bar = 1.f - FMath::Clamp(Distance / 4500.f, 0.f, 1.f);
		Out.BarLabel = TEXT("Closing in");
		if (Distance < 190.f)
		{
			Out.Center = TEXT("[F] TACKLE");
		}
	}
	Out.Lines.Add(FString::Printf(TEXT("Time left %s"), *Clock(TimeLimit - Elapsed())));
}

// Escape ------------------------------------------------------------------------------------------------------

void APuffersStepEscape::OnBegin()
{
	const int32 Count = Params.Count > 0 ? Params.Count : (GetDifficulty() > 0.6f ? 4 : 3);
	LoseDistance = Params.Distance > 0.f ? Params.Distance : 2600.f;
	LoseSeconds = Params.Seconds > 0.f ? Params.Seconds : 5.f;
	LostFor = 0.f;
	const EPuffersNPCRole Role = RoleFromParams(EPuffersNPCRole::Enforcer);
	const float Speed = Params.Speed > 0.f ? Params.Speed : 480.f + 70.f * GetDifficulty();
	APawn* Player = GetPlayerPawn();
	const FVector From = PlayerLocation();
	FVector Back = Player ? -Player->GetActorForwardVector() : FVector::BackwardVector;
	Back.Z = 0.f;
	const TCHAR* Name = Role == EPuffersNPCRole::Police ? TEXT("Officer") : Role == EPuffersNPCRole::Security ? TEXT("Security") : TEXT("Pursuer");
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = (i - (Count - 1) * 0.5f) * 35.f;
		const FVector At = From + Back.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal() * (1300.f + 150.f * (i % 3));
		if (APuffersNPCCharacter* NPC = SpawnNPC(At, Name, Role, 200.f, Speed, false))
		{
			Pursuers.Add(NPC);
			if (APuffersStepAIController* Brain = AI(NPC))
			{
				Brain->ChaseActor(Player);
			}
		}
	}
	StatusLine = TEXT("Break line of sight or outrun them. Crouch behind cover to hide.");
}

void APuffersStepEscape::OnRunning(float DeltaSeconds)
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
	}
	bool bAllLost = true;
	NearestPursuer = MAX_flt;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Pursuers)
	{
		APuffersNPCCharacter* NPC = Entry.Get();
		if (!NPC)
		{
			continue;
		}
		APuffersStepAIController* Brain = AI(NPC);
		if (Brain && Brain->GetMode() == EPuffersStepAIMode::Chase)
		{
			// The player may have jumped into a car: chase whatever they're driving.
			Brain->ChaseActor(Player);
		}
		const float Distance = FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation());
		NearestPursuer = FMath::Min(NearestPursuer, Distance);
		if (Distance < 140.f && !GetPlayerVehicle())
		{
			const FName Event = NPC->NPCRole == EPuffersNPCRole::Police ? FName(TEXT("CaughtByPolice"))
				: (NPC->NPCRole == EPuffersNPCRole::Security || NPC->NPCRole == EPuffersNPCRole::CorrectionalOfficer) ? FName(TEXT("CaughtByGuard"))
				: FName(TEXT("CaughtByNPC"));
			const FName Id = NPC->NPCId;
			if (BroadcastAndCheck(Event, Id))
			{
				Fail(TEXT("They caught you."));
			}
			return;
		}
		const bool bSees = Distance < 1400.f || (Distance < LoseDistance && ClearLine(NPC->GetActorLocation() + FVector(0.f, 0.f, 60.f),
			Player->GetActorLocation(), NPC, Player) && !IsPlayerCrouched());
		bAllLost &= !bSees;
	}

	LostFor = bAllLost ? LostFor + DeltaSeconds : FMath::Max(0.f, LostFor - DeltaSeconds * 2.f);
	if (LostFor >= LoseSeconds)
	{
		Succeed(TEXT("You lost them."));
		return;
	}
	if (Cast<APuffersObjectiveVolume>(AnchorActor.Get()) && Elapsed() > 3.f && PlayerDistance(Anchor) < 420.f)
	{
		Succeed(TEXT("Clean getaway."));
	}
}

void APuffersStepEscape::OnAbilityUsed(FName Ability)
{
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Pursuers)
	{
		APuffersStepAIController* Brain = AI(Entry.Get());
		if (!Brain)
		{
			continue;
		}
		if (Ability == TEXT("WrongTurn"))
		{
			// Everyone goes the wrong way for a few seconds.
			Brain->GoTo(RandomPointAround(Entry->GetActorLocation(), 900.f, 1500.f, 41), true);
			StatusLine = TEXT("Wrong turn! They went the other way.");
		}
		else if (Ability == TEXT("Trendsetter"))
		{
			Brain->Slow(5.f, 0.25f);
			StatusLine = TEXT("They can't stop staring at the fit.");
		}
	}
	if (Ability == TEXT("WrongTurn"))
	{
		APawn* Player = GetPlayerPawn();
		FTimerHandle Handle;
		TWeakObjectPtr<APuffersStepEscape> Self(this);
		GetWorldTimerManager().SetTimer(Handle, [Self, Player]()
		{
			if (!Self.IsValid() || Self->State != EPuffersStepState::Running)
			{
				return;
			}
			for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Self->Pursuers)
			{
				if (APuffersStepAIController* Brain = AI(Entry.Get()))
				{
					Brain->ChaseActor(Player);
				}
			}
		}, 4.f, false);
	}
}

void APuffersStepEscape::OnEnd()
{
	Pursuers.Reset();
}

void APuffersStepEscape::FillOverlay(FPuffersStepOverlay& Out) const
{
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Pursuers)
	{
		if (const APuffersNPCCharacter* NPC = Entry.Get())
		{
			Out.Markers.Add(MakeMarker(NPC->GetActorLocation(), TEXT("!"), Red, false));
		}
	}
	if (Cast<APuffersObjectiveVolume>(AnchorActor.Get()))
	{
		Out.Markers.Add(MakeMarker(Anchor, TEXT("EXIT"), Green));
	}
	Out.Bar = LoseSeconds > 0.f ? LostFor / LoseSeconds : 0.f;
	Out.BarLabel = TEXT("Losing them");
	Out.BarColor = Green;
	if (NearestPursuer < MAX_flt)
	{
		Out.Lines.Add(FString::Printf(TEXT("Closest pursuer %dm"), FMath::RoundToInt(NearestPursuer / 100.f)));
	}
}

// Follow (Tail / Escort) -----------------------------------------------------------------------------------------

FString APuffersStepFollow::GetGameplayLabel() const
{
	return IsEscort() ? TEXT("ESCORT") : TEXT("TAIL");
}

void APuffersStepFollow::OnBegin()
{
	Suspicion = 0.f;
	LostFor = 0.f;
	Mobbed = 0.f;
	Shoves = 0;
	Crowd.Reset();
	const FString Named = SpeakerName();

	if (IsEscort())
	{
		const float Distance = Params.Distance > 0.f ? Params.Distance : 3000.f;
		Target = SpawnNPC(Anchor, Named.IsEmpty() ? TEXT("VIP") : Named, EPuffersNPCRole::Pedestrian, 200.f, 450.f, false);
		if (APuffersStepAIController* Brain = AI(Target.Get()))
		{
			Brain->FollowActor(GetPlayerPawn(), 230.f);
		}
		Destination = RandomPointAround(Anchor, Distance * 0.7f, Distance, 17);
		SpawnMarker(Destination, TEXT("Cylinder"), FVector(3.f, 3.f, 0.05f), Green, TEXT("DESTINATION"));
		const int32 Fans = Params.Count > 0 ? Params.Count : 3;
		for (int32 i = 0; i < Fans; ++i)
		{
			const FVector Mid = FMath::Lerp(Anchor, Destination, 0.25f + 0.2f * i);
			if (APuffersNPCCharacter* Fan = SpawnNPC(RandomPointAround(Mid, 500.f, 900.f, 60 + i), TEXT("Fan"), EPuffersNPCRole::Pedestrian,
				180.f, 380.f + 40.f * GetDifficulty(), false))
			{
				Crowd.Add(Fan);
				if (APuffersStepAIController* Brain = AI(Fan))
				{
					Brain->ChaseActor(Target.Get());
				}
			}
		}
		StatusLine = Params.bPeaceful ? TEXT("Walk them to the destination. Keep fans off them: press E near a fan to wave them off.")
			: TEXT("Walk them to the destination. Keep fans off them: shove (F) or wave them off (E).");
		return;
	}

	Target = SpawnNPC(Anchor, Named.IsEmpty() ? TEXT("Mark") : Named, RoleFromParams(EPuffersNPCRole::Pedestrian),
		Params.Speed > 0.f ? Params.Speed : 190.f, 420.f, false);
	if (APuffersStepAIController* Brain = AI(Target.Get()))
	{
		Brain->SetRoute(MakeRoute(Anchor, Params.Count > 0 ? Params.Count : 7, 800.f, 1300.f, 7), false);
	}
	LookBackTimer = 8.f;
	StatusLine = TEXT("Keep them in sight but don't get close. Crouch (C) when they look back.");
}

void APuffersStepFollow::OnRunning(float DeltaSeconds)
{
	APuffersNPCCharacter* Mark = Target.Get();
	APawn* Player = GetPlayerPawn();
	if (!Mark || !Player)
	{
		Fail(TEXT("Lost them."));
		return;
	}
	APuffersStepAIController* Brain = AI(Mark);
	Gap = FVector::Dist2D(Mark->GetActorLocation(), Player->GetActorLocation());

	if (IsEscort())
	{
		if (FVector::Dist2D(Mark->GetActorLocation(), Destination) < 380.f)
		{
			Succeed(TEXT("Delivered safe."));
			return;
		}
		int32 OnThem = 0;
		for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Crowd)
		{
			APuffersNPCCharacter* Fan = Entry.Get();
			if (!Fan)
			{
				continue;
			}
			if (FVector::Dist2D(Fan->GetActorLocation(), Mark->GetActorLocation()) < 170.f)
			{
				++OnThem;
			}
			if (InteractPressed() && FVector::Dist2D(Fan->GetActorLocation(), Player->GetActorLocation()) < 220.f)
			{
				if (APuffersStepAIController* FanBrain = AI(Fan))
				{
					FanBrain->GoTo(RandomPointAround(Fan->GetActorLocation(), 1500.f, 2200.f, 90), false);
				}
			}
		}
		Mobbed = FMath::Clamp(Mobbed + (OnThem > 0 ? 0.22f * OnThem : -0.08f) * DeltaSeconds, 0.f, 1.f);
		if (Mobbed >= 1.f)
		{
			Fail(TEXT("They got swarmed."));
		}
		return;
	}

	// Tail
	if (Brain && Brain->HasFinishedRoute())
	{
		Succeed(TEXT("You saw where they went."));
		return;
	}
	LookBackTimer -= DeltaSeconds;
	if (LookBackTimer <= 0.f && Brain)
	{
		Brain->Pause(2.5f, Player);
		LookBackTimer = FMath::Lerp(10.f, 6.f, GetDifficulty()) + 2.5f;
	}
	const bool bLookingBack = Brain && Brain->IsPaused();
	const FVector ToPlayer = (Player->GetActorLocation() - Mark->GetActorLocation()).GetSafeNormal2D();
	const bool bInView = FVector::DotProduct(Mark->GetActorForwardVector(), ToPlayer) > 0.5f;
	const bool bSeen = (bLookingBack || bInView) && Gap < 1600.f
		&& ClearLine(Mark->GetActorLocation() + FVector(0.f, 0.f, 60.f), Player->GetActorLocation(), Mark, Player);
	float Gain = 0.f;
	if (Gap < 350.f)
	{
		Gain = 0.9f;
	}
	else if (bSeen)
	{
		Gain = (bLookingBack ? 0.55f : 0.25f) * (Gap < 900.f ? 1.6f : 1.f) * (IsPlayerCrouched() ? 0.35f : 1.f);
	}
	Gain *= FMath::Lerp(0.7f, 1.3f, GetDifficulty());
	Suspicion = FMath::Clamp(Suspicion + (Gain > 0.f ? Gain : -0.12f) * DeltaSeconds, 0.f, 1.f);
	if (Suspicion >= 1.f)
	{
		Fail(TEXT("They made you."));
		return;
	}
	const float MaxGap = Params.Distance > 0.f ? Params.Distance : 2200.f;
	LostFor = Gap > MaxGap ? LostFor + DeltaSeconds : 0.f;
	if (LostFor > 6.f)
	{
		Fail(TEXT("You lost them."));
	}
}

void APuffersStepFollow::OnGameEvent(FName EventName, FName Context)
{
	if (State != EPuffersStepState::Running || EventName != TEXT("ShovedNPC"))
	{
		return;
	}
	const APuffersNPCCharacter* Mark = Target.Get();
	if (Mark && Context == Mark->NPCId)
	{
		Fail(IsEscort() ? TEXT("You shoved the person you're protecting.") : TEXT("You blew your cover."));
		return;
	}
	if (!IsEscort())
	{
		return;
	}
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Crowd)
	{
		APuffersNPCCharacter* Fan = Entry.Get();
		if (Fan && Fan->NPCId == Context)
		{
			if (APuffersStepAIController* Brain = AI(Fan))
			{
				Brain->Slow(6.f, 0.2f);
			}
			if (Params.bPeaceful && ++Shoves > 2)
			{
				Fail(TEXT("Too rough. This was supposed to stay peaceful."));
			}
			return;
		}
	}
}

void APuffersStepFollow::OnEnd()
{
	Target.Reset();
	Crowd.Reset();
}

void APuffersStepFollow::FillOverlay(FPuffersStepOverlay& Out) const
{
	if (const APuffersNPCCharacter* Mark = Target.Get())
	{
		Out.Markers.Add(MakeMarker(Mark->GetActorLocation(), IsEscort() ? TEXT("VIP") : TEXT("MARK"), Gold, false));
	}
	if (IsEscort())
	{
		Out.Markers.Add(MakeMarker(Destination, TEXT("DESTINATION"), Green));
		for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Crowd)
		{
			if (const APuffersNPCCharacter* Fan = Entry.Get())
			{
				Out.Markers.Add(MakeMarker(Fan->GetActorLocation(), TEXT("FAN"), Red, false));
			}
		}
		Out.Bar = Mobbed;
		Out.BarLabel = TEXT("Mobbed");
		Out.BarColor = Red;
		return;
	}
	Out.Bar = Suspicion;
	Out.BarLabel = Params.Label.IsEmpty() ? TEXT("Suspicion") : Params.Label;
	Out.BarColor = Red;
	const float MaxGap = Params.Distance > 0.f ? Params.Distance : 2200.f;
	Out.Lines.Add(Gap < 350.f ? TEXT("Too close!") : Gap > MaxGap ? TEXT("Falling behind...") : TEXT("Good distance."));
	const APuffersStepAIController* Brain = AI(Target.Get());
	if (Brain && Brain->IsPaused())
	{
		Out.Center = TEXT("THEY'RE LOOKING BACK");
	}
}

// Stealth / Approach -------------------------------------------------------------------------------------------

FString APuffersStepStealth::GetGameplayLabel() const
{
	return IsApproach() ? TEXT("APPROACH") : TEXT("STEALTH");
}

void APuffersStepStealth::OnBegin()
{
	Detection = 0.f;
	DistractedUntil = 0.f;
	Guards.Reset();
	Goal = Anchor;
	const int32 Count = Params.Count > 0 ? Params.Count : (IsApproach() ? 0 : 3);
	const EPuffersNPCRole Role = RoleFromParams(EPuffersNPCRole::Security);
	const TCHAR* Name = Role == EPuffersNPCRole::Enforcer ? TEXT("Crew") : Role == EPuffersNPCRole::Police ? TEXT("Officer") : TEXT("Guard");
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Center = Goal + FVector(0.f, 0.f, 0.f);
		TArray<FVector> Loop;
		for (int32 p = 0; p < 3; ++p)
		{
			Loop.Add(RandomPointAround(Center, 450.f + 250.f * i, 900.f + 300.f * i, 100 + i * 10 + p));
		}
		if (APuffersNPCCharacter* Guard = SpawnNPC(Loop[0], Name, Role, 140.f + 30.f * GetDifficulty(), 400.f, false))
		{
			Guards.Add(Guard);
			if (APuffersStepAIController* Brain = AI(Guard))
			{
				Brain->SetPatrol(Loop, false);
			}
		}
	}
	if (IsApproach())
	{
		const FString Named = SpeakerName();
		Mark = SpawnNPC(Anchor, Named.IsEmpty() ? TEXT("Mark") : Named, RoleFromParams(EPuffersNPCRole::Pedestrian), 130.f, 420.f, false);
		if (APuffersStepAIController* Brain = AI(Mark.Get()))
		{
			TArray<FVector> Loop;
			for (int32 p = 0; p < 4; ++p)
			{
				Loop.Add(RandomPointAround(Anchor, 300.f, 800.f, 200 + p));
			}
			Brain->SetPatrol(Loop, false);
		}
		StatusLine = TEXT("Come up from behind. Crouch (C) to stay low, then press E when you're close.");
	}
	else
	{
		StatusLine = TEXT("Stay out of their sight cones. Crouch (C) to be harder to see.");
	}
}

float APuffersStepStealth::Watch(APuffersNPCCharacter* Watcher, float Range, float HalfAngle) const
{
	const APawn* Player = GetPlayerPawn();
	if (!Watcher || !Player)
	{
		return 0.f;
	}
	const FVector To = Player->GetActorLocation() - Watcher->GetActorLocation();
	const float Distance = To.Size2D();
	if (Distance > Range)
	{
		return 0.f;
	}
	const float Cos = FVector::DotProduct(Watcher->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D());
	if (Cos < FMath::Cos(FMath::DegreesToRadians(HalfAngle)) && Distance > 220.f)
	{
		return 0.f;
	}
	if (!ClearLine(Watcher->GetActorLocation() + FVector(0.f, 0.f, 60.f), Player->GetActorLocation(), Watcher, Player))
	{
		return 0.f;
	}
	return FPuffersStepRules::DetectionRate(Distance, Range, IsPlayerCrouched(), GetDifficulty());
}

void APuffersStepStealth::OnRunning(float DeltaSeconds)
{
	const bool bDistracted = GetWorld()->GetTimeSeconds() < DistractedUntil || IsAbilityActive(TEXT("Trendsetter"));
	float Rate = 0.f;
	APuffersNPCCharacter* Worst = nullptr;
	TArray<APuffersNPCCharacter*> Watchers;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Guards)
	{
		Watchers.Add(Entry.Get());
	}
	if (Mark.IsValid())
	{
		Watchers.Add(Mark.Get());
	}
	for (APuffersNPCCharacter* Watcher : Watchers)
	{
		if (!Watcher || bDistracted)
		{
			continue;
		}
		const float R = Watch(Watcher, Watcher == Mark.Get() ? 1100.f : 1500.f, 55.f);
		if (R > Rate)
		{
			Rate = R;
			Worst = Watcher;
		}
#if ENABLE_DRAW_DEBUG
		DrawDebugCone(GetWorld(), Watcher->GetActorLocation(), Watcher->GetActorForwardVector(), Watcher == Mark.Get() ? 1100.f : 1500.f,
			FMath::DegreesToRadians(55.f), FMath::DegreesToRadians(10.f), 12, R > 0.f ? FColor::Red : FColor(255, 200, 60), false, -1.f, 0, 1.5f);
#endif
	}
	Detection = FMath::Clamp(Detection + (Rate > 0.f ? Rate : -0.25f) * DeltaSeconds, 0.f, 1.f);
	if (Worst && Detection > 0.5f)
	{
		if (APuffersStepAIController* Brain = AI(Worst))
		{
			if (!Brain->IsPaused())
			{
				Brain->Pause(1.f, GetPlayerPawn());
			}
		}
	}
	if (Detection >= 1.f)
	{
		Fail(IsApproach() ? TEXT("They saw you coming.") : (Params.Label.IsEmpty() ? TEXT("Spotted.") : TEXT("It got heated.")), true);
		return;
	}

	if (IsApproach())
	{
		const APuffersNPCCharacter* Target = Mark.Get();
		if (Target && PlayerDistance(Target->GetActorLocation()) < 210.f && InteractPressed())
		{
			Succeed(TEXT("Handled. No scene."));
		}
		return;
	}
	if (PlayerDistance(Goal) < 260.f)
	{
		Succeed(TEXT("You made it through."));
	}
}

void APuffersStepStealth::OnAbilityUsed(FName Ability)
{
	if (Ability == TEXT("WrongTurn") || Ability == TEXT("Trendsetter") || Ability == TEXT("TalkThatShit"))
	{
		DistractedUntil = GetWorld()->GetTimeSeconds() + 5.f;
		StatusLine = TEXT("They're distracted. Move!");
	}
}

void APuffersStepStealth::OnEnd()
{
	Guards.Reset();
	Mark.Reset();
}

void APuffersStepStealth::FillOverlay(FPuffersStepOverlay& Out) const
{
	Out.Bar = Detection;
	Out.BarLabel = Params.Label.IsEmpty() ? (IsApproach() ? TEXT("Noticed") : TEXT("Detection")) : Params.Label;
	Out.BarColor = Red;
	for (const TWeakObjectPtr<APuffersNPCCharacter>& Entry : Guards)
	{
		if (const APuffersNPCCharacter* Guard = Entry.Get())
		{
			Out.Markers.Add(MakeMarker(Guard->GetActorLocation(), TEXT("o_o"), Red, false));
		}
	}
	if (const APuffersNPCCharacter* Target = Mark.Get())
	{
		Out.Markers.Add(MakeMarker(Target->GetActorLocation(), PlayerDistance(Target->GetActorLocation()) < 210.f ? TEXT("[E]") : TEXT("MARK"), Gold, false));
	}
	else
	{
		Out.Markers.Add(MakeMarker(Goal, TEXT("GET HERE"), Green));
	}
	if (Detection > 0.6f)
	{
		Out.Center = TEXT("?!");
	}
}

// LoseHeat -----------------------------------------------------------------------------------------------------

void APuffersStepLoseHeat::OnBegin()
{
	if (UPuffersHeatSubsystem* Heat = GetWorld()->GetSubsystem<UPuffersHeatSubsystem>())
	{
		Heat->RaiseHeat(FMath::Max(2, Params.Count), Objective.Id);
	}
	StatusLine = TEXT("Stay out of sight until the heat cools off.");
}

void APuffersStepLoseHeat::OnRunning(float)
{
	const UPuffersHeatSubsystem* Heat = GetWorld()->GetSubsystem<UPuffersHeatSubsystem>();
	if (!Heat || Heat->GetHeatLevel() == 0)
	{
		Succeed(TEXT("Heat's off."));
	}
}

void APuffersStepLoseHeat::FillOverlay(FPuffersStepOverlay& Out) const
{
	if (const UPuffersHeatSubsystem* Heat = GetWorld()->GetSubsystem<UPuffersHeatSubsystem>())
	{
		Out.Lines.Add(FString::Printf(TEXT("Heat %d"), Heat->GetHeatLevel()));
		Out.Bar = Heat->GetHeatLevel() / static_cast<float>(UPuffersHeatSubsystem::MaxHeat);
		Out.BarLabel = TEXT("Heat");
		Out.BarColor = Red;
	}
}
