// Data for mission step gameplay. Every objective in the mission JSON has a Type (TALK, CHASE, ...). The step
// system turns each Type into real gameplay by picking a "Gameplay" runner and its tuning from
// Content/Data/Steps/StepRules.json: type defaults, then keyword rules matched against the description,
// then per-objective overrides. The mission JSON itself is never changed.

#pragma once

#include "CoreMinimal.h"
#include "PuffersStepTypes.generated.h"

// Resolved tuning for one objective's gameplay. Every field is optional in the JSON.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersStepParams
{
	GENERATED_BODY()

	// Which runner plays this step: Reach, EnterVehicle, Route, Delivery, Hold, Timing, Mash, Rhythm, Memory,
	// Conversation, Persuade, Negotiate, BusinessMenu, Tail, Escort, Chase, Escape, Stealth, Approach, Search,
	// Photo, Video, Defend, Combat, Switch, Scan, LoseHeat, Prop (stand-in prop, no runner).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Gameplay = TEXT("Prop");

	// Immediate (as soon as the stage starts), Interact (press E on the step's prop or NPC), Proximity (walk up).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName StartMode = TEXT("Interact");

	// Seconds: hold time, take length, defend duration, time limit... depends on the runner. 0 = runner default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Seconds = 0.f;

	// Count: pursuers, guards, waves, hits, clues... 0 = runner default (often the objective's RequiredCount).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Count = 0;

	// Distance in cm: drop-off distance, lose distance, follow distance. 0 = runner default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Distance = 0.f;

	// Radius in cm: zone, search area, arrival radius. 0 = runner default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Radius = 0.f;

	// Speed in cm/s for the step's NPC (fleeing target, pursuers). 0 = runner default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Speed = 0.f;

	// 0 easy .. 1 hard. Raised by choice-flag reactions (Reactions.json DifficultyDelta).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Difficulty = 0.4f;

	// NPC role for spawned people: Pedestrian, Security, Police, Enforcer, Inmate, CorrectionalOfficer.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Role = TEXT("Pedestrian");

	// Character to switch to (Switch), or the speaker's name for spawned NPCs. Empty = taken from the description.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Target;

	// Display label for the runner's meter, e.g. "Tension" instead of "Detection".
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Label;

	// Abilities that help on this step, best first. Z uses the first one the player has.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Abilities;

	// Reach/Route/Chase: must be done in a vehicle.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bVehicle = false;

	// Defend/Escort: shoving people counts against you ("keep it peaceful").
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bPeaceful = false;

	// Delivery: the package breaks if you sprint, jump or crash.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bFragile = false;

	// Video: a livestream (viewer count instead of clean takes).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bLive = false;

	// Photo/Video subjects: NPC (people) or Spot (places).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Subject = TEXT("NPC");

	// Raise heat to at least this level when the step starts (shots fired, responders). 0 = no change.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Heat = 0;

	// Ability granted only while this step runs (M01's Mac Vision sweep before the ability is earned).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName GrantAbility;
};

// A choice-flag reaction from Content/Data/Steps/Reactions.json: when the save has Flag, the line plays
// at the start of the listed missions (or when the named objective's conversation opens) and can make
// that mission's steps harder or easier.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersStepReaction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Flag;

	// Empty = any mission.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> Missions;

	// Empty = shown when the mission starts. Set = opens that objective's conversation.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Objective;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Line;

	// Added to Difficulty for every step of the listed missions while the flag is set.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DifficultyDelta = 0.f;
};

// What a runner wants drawn this frame. The step subsystem renders it on top of the mission HUD.
struct FPuffersStepMarker
{
	FVector Location = FVector::ZeroVector;
	FString Text;
	FLinearColor Color = FLinearColor::White;
	bool bShowDistance = true;
};

struct FPuffersStepOverlay
{
	FString Header;
	TArray<FString> Lines;
	// 0..1 meter, or < 0 for none.
	float Bar = -1.f;
	FString BarLabel;
	FLinearColor BarColor = FLinearColor(1.f, 0.77f, 0.25f);
	// Second meter (health, damage, tension).
	float Bar2 = -1.f;
	FString Bar2Label;
	FLinearColor Bar2Color = FLinearColor(1.f, 0.3f, 0.25f);
	// Large centred text (a prompt arrow, "SPOTTED", a countdown).
	FString Center;
	// Menu / dialogue options; the player presses 1-4.
	TArray<FString> Options;
	TArray<FPuffersStepMarker> Markers;
	bool bPhoneFrame = false;
	bool bRecording = false;
	bool bCrosshair = false;
};
