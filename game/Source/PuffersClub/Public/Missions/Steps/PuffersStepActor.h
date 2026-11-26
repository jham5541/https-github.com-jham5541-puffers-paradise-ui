// Step runners: one actor per active objective that turns its Type into gameplay. The step subsystem spawns
// them when a stage starts and destroys them when it ends. A runner waits (Pending) until the player starts it
// (press E on its prop or NPC, walk up, or right away), plays its gameplay (Running), and reports progress to
// the mission subsystem when the player succeeds. Failing a step resets it so the player can try again; only
// events the mission lists in FailEvents (CaughtByPolice, CaughtByGuard...) fail the whole mission.
//
// Each runner class implements one or more "Gameplay" names from StepRules.json. All 77+ missions use them.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "AI/PuffersNPCCharacter.h"
#include "Missions/PuffersMissionTypes.h"
#include "Missions/Steps/PuffersStepTypes.h"
#include "PuffersStepActor.generated.h"

class APlayerController;
class APuffersObjectiveVolume;
class APuffersPlayerCharacter;
class APuffersStepAIController;
class APuffersVehicle;
class UPuffersInteractableComponent;
class UPuffersMissionSubsystem;
class UPuffersStepSubsystem;
class UStaticMeshComponent;
class UTextRenderComponent;

// A simple visible thing a step puts in the world: drop-off rings, clues, spots, zones, packages.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepMarker : public AActor
{
	GENERATED_BODY()

public:
	APuffersStepMarker();

	UPROPERTY(VisibleAnywhere, Category = "Step")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Step")
	TObjectPtr<UTextRenderComponent> Label;

	// Shape: Cylinder, Cube, Sphere, Cone. Scale is applied to the mesh only.
	void Setup(const FString& Shape, const FVector& Scale, const FLinearColor& Color, const FString& Text);
	void SetColor(const FLinearColor& Color);
	void SetText(const FString& Text);

	virtual void Tick(float DeltaSeconds) override;
};

UENUM()
enum class EPuffersStepState : uint8
{
	Pending,
	Running,
	Cooldown,
	Done
};

UCLASS(Abstract, NotPlaceable)
class PUFFERSCLUB_API APuffersStepActor : public AActor
{
	GENERATED_BODY()

public:
	APuffersStepActor();

	void Init(UPuffersStepSubsystem* InOwner, FName InMissionId, const FPuffersObjectiveDefinition& InObjective,
		const FPuffersStepParams& InParams, int32 InGeneration, AActor* InAnchorActor, const FVector& InAnchor);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FName GetMissionId() const { return MissionId; }
	FName GetObjectiveId() const { return Objective.Id; }
	FName GetChoiceGroup() const { return Objective.ChoiceGroup; }
	int32 GetGeneration() const { return Generation; }
	EPuffersStepState GetState() const { return State; }
	const FPuffersStepParams& GetParams() const { return Params; }
	const FPuffersObjectiveDefinition& GetObjective() const { return Objective; }
	FVector GetAnchor() const { return Anchor; }

	// Fills what the subsystem draws for this runner this frame.
	void BuildOverlay(FPuffersStepOverlay& Out) const;

	// Mission event bus, forwarded by the subsystem while this runner exists.
	virtual void OnGameEvent(FName EventName, FName Context) {}

	// Z was pressed and this ability fired.
	virtual void OnAbilityUsed(FName Ability) {}

	// While true the subsystem blocks keys 1-4 / d-pad from their normal actions (wave, sit, minimap).
	virtual bool WantsMenuInput() const { return false; }

	// The gameplay label shown in the panel header ("CHASE", "STEALTH"...).
	virtual FString GetGameplayLabel() const;

	// Lets a choice-group menu (BusinessMenu) stand in for its sibling options.
	virtual bool CoversObjective(FName ObjectiveId) const { return ObjectiveId == Objective.Id; }

protected:
	// Called once after Init, before the player starts the step (spawn a speaker, place a pickup...).
	virtual void OnPrepare() {}
	// Gameplay begins.
	virtual void OnBegin() {}
	virtual void OnRunning(float DeltaSeconds) {}
	// Gameplay ends (success, failure, or the stage moved on). Clean up anything temporary.
	virtual void OnEnd() {}
	virtual void FillOverlay(FPuffersStepOverlay& Out) const {}

	void Begin();
	// Reports one unit of progress (or Amount). The runner resets for the next unit if more are needed.
	void Succeed(const FString& Message = FString(), int32 Amount = 1);
	// Resets the step after a short cooldown. bReturnPlayer puts the player back where the attempt started.
	void Fail(const FString& Reason, bool bReturnPlayer = false);
	// Reports progress for another objective (a BusinessMenu picking one option of its group).
	bool ReportFor(FName OtherObjectiveId, int32 Amount = 1);
	void Toast(const FString& Text, bool bAlert = false) const;
	// Broadcast on the mission event bus. Returns false if that ended this step's stage (mission failed).
	bool BroadcastAndCheck(FName EventName, FName Context);

	UPuffersMissionSubsystem* GetMissions() const;
	APlayerController* GetPC() const;
	APawn* GetPlayerPawn() const;
	APuffersPlayerCharacter* GetPlayerCharacter() const;
	APuffersVehicle* GetPlayerVehicle() const;
	bool IsPlayerCrouched() const;
	bool IsPlayerSprinting() const;
	float PlayerDistance(const FVector& Location) const;
	FVector PlayerLocation() const;

	bool KeyPressed(const FKey& Key) const;
	bool KeyDown(const FKey& Key) const;
	bool InteractPressed() const;
	bool InteractDown() const;
	bool FirePressed() const;
	bool FireDown() const;
	bool AimDown() const;
	// 1-4 from the number keys or the d-pad (up, right, down, left), else 0.
	int32 OptionPressed() const;

	bool HasAbility(FName Ability) const;
	bool IsAbilityActive(FName Ability) const;
	float GetDifficulty() const;
	int32 GetRequired() const { return FMath::Max(1, Objective.RequiredCount); }
	int32 GetProgress() const;
	int32 GetRemaining() const { return FMath::Max(1, GetRequired() - GetProgress()); }
	float Elapsed() const { return RunTime; }

	FVector GroundPoint(const FVector& Near) const;
	FVector RandomPointAround(const FVector& Center, float MinRadius, float MaxRadius, int32 Salt) const;
	TArray<FVector> MakeRoute(const FVector& From, int32 NumPoints, float StepMin, float StepMax, int32 Salt) const;
	bool ClearLine(const FVector& From, const FVector& To, const AActor* IgnoreA, const AActor* IgnoreB) const;
	// Angle in degrees between the camera's forward and the direction to Location, and the distance.
	bool CameraAngleTo(const FVector& Location, float& OutAngle, float& OutDistance) const;

	// Temporary actors are destroyed when the attempt ends; persistent ones live until the stage ends.
	APuffersNPCCharacter* SpawnNPC(const FVector& At, const FString& Name, EPuffersNPCRole Role, float WalkSpeed, float RunSpeed,
		bool bTalkable, bool bPersistent = false);
	APuffersStepMarker* SpawnMarker(const FVector& At, const FString& Shape, const FVector& Scale, const FLinearColor& Color,
		const FString& Text, bool bPersistent = false);
	static APuffersStepAIController* AI(APuffersNPCCharacter* NPC);
	EPuffersNPCRole RoleFromParams(EPuffersNPCRole Fallback) const;
	FString SpeakerName() const;

	void LockMovement(bool bLock);
	void ReturnPlayerToStart();
	void ClearTemporary();
	// Takes over another objective's stand-in prop so pressing E on it starts this step instead.
	void HijackObjectiveProp(FName OtherObjectiveId);
	// Pressing E on this interactable (a spawned speaker) starts the step too.
	void ListenTo(UPuffersInteractableComponent* Component);
	// Leaves without success or failure (Backspace out of a conversation).
	void Leave();

	FName MissionId;
	FPuffersObjectiveDefinition Objective;
	FPuffersStepParams Params;
	int32 Generation = 0;
	FVector Anchor = FVector::ZeroVector;
	FVector StartLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> AnchorActor;
	TWeakObjectPtr<UPuffersStepSubsystem> Owner;
	EPuffersStepState State = EPuffersStepState::Pending;
	float RunTime = 0.f;
	float CooldownLeft = 0.f;
	FString StatusLine;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Temporary;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Persistent;

private:
	UFUNCTION()
	void OnAnchorUsed(AActor* Interactor);

	void TryStart();
	void HijackAnchor();
	void ReleaseAnchor();

	struct FHijack
	{
		TWeakObjectPtr<UPuffersInteractableComponent> Component;
		FName SavedObjective;
	};
	TArray<FHijack> Hijacked;
	TWeakObjectPtr<APuffersObjectiveVolume> HijackedVolume;
	TWeakObjectPtr<APuffersNPCCharacter> HijackedTalker;
	bool bStartRequested = false;
	bool bMovementLocked = false;
};

// Reach (GO_TO_LOCATION, optionally in a vehicle), Route (DRIVE_ROUTE checkpoints), EnterVehicle.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepTravel : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	TArray<FVector> Checkpoints;
	int32 NextCheckpoint = 0;
	float TimeLimit = 0.f;
	TWeakObjectPtr<APuffersStepMarker> CheckpointMarker;
};

// Delivery (DELIVER_ITEM): pick up at the step's spot, drop off at one or more places, optionally fragile or timed.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepDelivery : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("DELIVERY"); }

private:
	void NextDrop();

	bool bCarrying = false;
	FVector DropPoint = FVector::ZeroVector;
	float Damage = 0.f;
	float TimeLimit = 0.f;
	TWeakObjectPtr<APuffersStepMarker> DropMarker;
	TWeakObjectPtr<APuffersStepMarker> Package;
};

// Chase (CHASE, "tackle"/"catch"): a target runs a route; get close and tackle (F or E) before they get away.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepChase : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual void OnGameEvent(FName EventName, FName Context) override;
	virtual void OnAbilityUsed(FName Ability) override;

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("CHASE"); }

private:
	TWeakObjectPtr<APuffersNPCCharacter> Runner;
	float TimeLimit = 45.f;
	float GaspTimer = 0.f;
	float Panic = 0.f;
};

// Escape (ESCAPE): pursuers spawn and run you down. Lose them (out of range or out of sight) or reach the exit.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepEscape : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual void OnAbilityUsed(FName Ability) override;

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("ESCAPE"); }

private:
	TArray<TWeakObjectPtr<APuffersNPCCharacter>> Pursuers;
	float LostFor = 0.f;
	float LoseSeconds = 5.f;
	float LoseDistance = 2600.f;
	float NearestPursuer = 0.f;
};

// Tail (follow at a distance without being made) and Escort (bring someone to a place, keeping a crowd off them).
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepFollow : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual void OnGameEvent(FName EventName, FName Context) override;

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	bool IsEscort() const { return Params.Gameplay == TEXT("Escort"); }

	TWeakObjectPtr<APuffersNPCCharacter> Target;
	TArray<TWeakObjectPtr<APuffersNPCCharacter>> Crowd;
	FVector Destination = FVector::ZeroVector;
	float Suspicion = 0.f;
	float LostFor = 0.f;
	float LookBackTimer = 8.f;
	float Mobbed = 0.f;
	int32 Shoves = 0;
	float Gap = 0.f;
};

// Stealth (reach a point past guards) and Approach (get to a person without them noticing, then press E).
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepStealth : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual void OnAbilityUsed(FName Ability) override;

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	bool IsApproach() const { return Params.Gameplay == TEXT("Approach"); }
	float Watch(APuffersNPCCharacter* Watcher, float Range, float HalfAngle) const;

	TArray<TWeakObjectPtr<APuffersNPCCharacter>> Guards;
	TWeakObjectPtr<APuffersNPCCharacter> Mark;
	FVector Goal = FVector::ZeroVector;
	float Detection = 0.f;
	float DistractedUntil = 0.f;
};

// LoseHeat: heat goes up, and the step completes once it's back to zero.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepLoseHeat : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("LOSE THE HEAT"); }
};

// Mini-games at a prop: Hold (hold E), Timing (stop the needle in the zone), Mash (tap E fast),
// Rhythm (hit the arrows on the beat), Memory (repeat the arrow sequence).
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepMinigame : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual bool WantsMenuInput() const override { return State == EPuffersStepState::Running; }

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	int32 ArrowPressed() const;
	void NewMemoryRound();
	void NextBeat();
	static FString ArrowName(int32 Arrow);

	float Meter = 0.f;
	int32 Hits = 0;
	int32 Misses = 0;
	int32 Goal = 3;
	float ZoneCenter = 0.5f;
	float ZoneWidth = 0.2f;
	float NeedleTime = 0.f;
	float Flash = 0.f;
	FString FlashText;
	// Rhythm / Memory
	TArray<int32> Sequence;
	int32 SeqIndex = 0;
	int32 Round = 0;
	bool bShowing = false;
	float BeatTimer = 0.f;
	int32 CurrentBeat = 0;
	bool bBeatOpen = false;
	int32 Beats = 0;
};

// Conversations: Conversation (pick how you play it), Persuade (win them over before they lose patience),
// Negotiate (haggle under their walk-away price), BusinessMenu (choose one option of a business decision).
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepDialogue : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual bool WantsMenuInput() const override { return State == EPuffersStepState::Running; }
	virtual bool CoversObjective(FName ObjectiveId) const override;

protected:
	virtual void OnPrepare() override;
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	void Pick(int32 Option);
	bool HasTalkAbility() const;

	TArray<FString> Lines;        // conversation so far (speaker lines and your picks)
	TArray<FString> Options;
	TArray<FName> MenuObjectives; // BusinessMenu: objective per option
	int32 Turn = 0;
	int32 Interest = 0;
	int32 Patience = 3;
	int32 Preferred = 0;
	int32 Disliked = 1;
	bool bAbilityUsed = false;
	float Offer = 0.6f;
	float Floor = 0.8f;
	int32 Ask = 500;
	float EndDelay = -1.f;
	bool bWon = false;
};

// Switch (CHARACTER_SWITCH): swap to the named Puffer.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepSwitch : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("SWITCH"); }

private:
	FName TargetCharacter;
};

// Search (SEARCH_AREA): clues and decoys hidden around an area; warmer/colder meter; Mac Vision reveals them.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepSearch : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("SEARCH"); }

private:
	struct FSpot
	{
		TWeakObjectPtr<APuffersStepMarker> Marker;
		FVector Location;
		bool bReal = false;
		bool bChecked = false;
	};
	TArray<FSpot> Spots;
	int32 Searching = INDEX_NONE;
	float SearchProgress = 0.f;
	float Warmth = 0.f;
};

// Phone camera: Photo (frame the subject and snap) and Video (keep the subject in frame for the whole take,
// or grow a livestream's viewers). P raises the phone, left mouse shoots or records.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepCamera : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override;

private:
	bool IsVideo() const { return Params.Gameplay == TEXT("Video"); }
	void SetPhone(bool bUp);
	int32 BestSubject(float& OutAngle, float& OutDistance) const;

	TArray<TWeakObjectPtr<AActor>> Subjects;
	TArray<bool> Captured;
	bool bPhoneUp = false;
	bool bRecording = false;
	float TakeTime = 0.f;
	float OutOfFrame = 0.f;
	float Viewers = 0.f;
	float LastQuality = -1.f;
	float SavedFOV = 90.f;
	float SavedArm = 380.f;
};

// Defend (DEFEND): hold a zone for a while. People push in; shove them out (F), or talk them down (E) when
// it has to stay peaceful.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepDefend : public APuffersStepActor
{
	GENERATED_BODY()

public:
	virtual void OnGameEvent(FName EventName, FName Context) override;
	virtual void OnAbilityUsed(FName Ability) override;

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("DEFEND"); }

private:
	void SpawnWave();
	void Repel(APuffersNPCCharacter* NPC);

	UFUNCTION()
	void OnAgitatorTalkedTo(AActor* Interactor);

	TArray<TWeakObjectPtr<APuffersNPCCharacter>> Agitators;
	TSet<FName> Repelled;
	float Integrity = 1.f;
	float Duration = 60.f;
	float WaveTimer = 0.f;
	int32 Wave = 0;
	int32 Violence = 0;
	float ZoneRadius = 650.f;
};

// Combat (shooting choices): armed enemies close in and fire. Aim at them and click; crouch to take less fire.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepCombat : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("FIGHT"); }

private:
	struct FEnemy
	{
		TWeakObjectPtr<APuffersNPCCharacter> NPC;
		int32 Health = 3;
		float NextShot = 0.f;
	};
	TArray<FEnemy> Enemies;
	float Health = 100.f;
	float LastHurt = -100.f;
	float FireCooldown = 0.f;
	float HitFlash = 0.f;
	float SavedFOV = 90.f;
	float SavedArm = 380.f;
	bool bAiming = false;
};

// Scan (Mac Vision sweeps): a crowd with a few real threats. Use Mac Vision (Z) or watch how people act,
// then look at a threat and press E to flag it. Too many false alarms fails the sweep.
UCLASS(NotPlaceable)
class PUFFERSCLUB_API APuffersStepScan : public APuffersStepActor
{
	GENERATED_BODY()

protected:
	virtual void OnBegin() override;
	virtual void OnRunning(float DeltaSeconds) override;
	virtual void OnEnd() override;
	virtual void FillOverlay(FPuffersStepOverlay& Out) const override;
	virtual FString GetGameplayLabel() const override { return TEXT("SWEEP"); }

private:
	int32 LookedAt(float MaxAngle) const;

	struct FPerson
	{
		TWeakObjectPtr<APuffersNPCCharacter> NPC;
		bool bThreat = false;
		bool bFlagged = false;
	};
	TArray<FPerson> People;
	int32 Flagged = 0;
	int32 Threats = 3;
	int32 FalseAlarms = 0;
	float TwitchTimer = 0.f;
};
