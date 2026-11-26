// Pure, engine-light rules for step gameplay, so they can be unit tested without a world:
// resolving an objective to its gameplay, and the scoring for each mini-game.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Missions/PuffersMissionTypes.h"
#include "Missions/Steps/PuffersStepTypes.h"

// StepRules.json, parsed once.
struct PUFFERSCLUB_API FPuffersStepCatalog
{
	// Missions left to their own hand-built level (BL01 lives in L_PuffingtonPen).
	TArray<FName> ExcludedMissions;

	// Type name (e.g. "CHASE") -> params object.
	TMap<FString, TSharedPtr<FJsonObject>> Defaults;

	struct FRule
	{
		TArray<FString> Types;      // empty = any type
		TArray<FString> Keywords;   // lower case; matched at the start of a word; empty = any description
		TArray<FName> Missions;     // empty = any mission
		int8 Choice = -1;           // "Choice": true = only choice-group options, false = never them, absent = either
		TSharedPtr<FJsonObject> Params;
	};
	TArray<FRule> Rules;

	// Objective id -> params object.
	TMap<FName, TSharedPtr<FJsonObject>> Overrides;

	TArray<FPuffersStepReaction> Reactions;

	bool LoadRulesFromString(const FString& Json, FString& OutError);
	bool LoadReactionsFromString(const FString& Json, FString& OutError);
};

struct PUFFERSCLUB_API FPuffersStepRules
{
	static FString TypeName(EPuffersObjectiveType Type);

	// True if Keyword appears in Text at the start of a word (case-insensitive). "chase" matches
	// "Chase him" and "the chase", not "purchase".
	static bool MatchesKeyword(const FString& Text, const FString& Keyword);

	// Index of the first rule that applies to this objective, or INDEX_NONE.
	static int32 FindRule(const FPuffersStepCatalog& Catalog, FName MissionId, const FPuffersObjectiveDefinition& Objective);

	// Type defaults, then the first matching keyword rule, then the objective's override.
	static FPuffersStepParams Resolve(const FPuffersStepCatalog& Catalog, FName MissionId, const FPuffersObjectiveDefinition& Objective);

	// Every runner name the code implements. Anything else plays as the stand-in prop.
	static bool IsKnownGameplay(FName Gameplay);

	// "Haze: get insulin..." / "As Kush, hold the crowd" -> "Haze" / "Kush" if it names a known character.
	static FName ParseCharacterPrefix(const FString& Description, const TArray<FName>& KnownCharacters);

	// "Switch to Lato" -> "Lato".
	static FName ParseSwitchTarget(const FString& Description, const TArray<FName>& KnownCharacters);

	// First known character named anywhere in the description (used to name a spawned speaker).
	static FName FindNamedCharacter(const FString& Description, const TArray<FName>& KnownCharacters);

	// Stable 0..N-1 pick from an id, so a conversation or haggle plays the same way every time.
	static int32 StableIndex(FName Id, int32 Salt, int32 N);

	// Persuasion: Tone 0..2 against the listener's preferred and disliked tones. Returns interest gained.
	static int32 PersuadeGain(int32 Tone, int32 Preferred, int32 Disliked);

	// Haggling: offer and the seller's hidden floor as fractions of the asking price.
	enum class EHaggle : uint8 { Deal, Warm, Cold };
	static EHaggle EvaluateOffer(float Offer, float Floor);

	// Phone photo quality 0..1 from how far off-centre (degrees) and how far away (cm) the subject is.
	static float PhotoQuality(float AngleDegrees, float Distance, bool bViral);

	// Timing bar: needle and zone centre in 0..1, zone width in 0..1.
	static bool IsNeedleInZone(float Needle, float ZoneCenter, float ZoneWidth);

	// Ping-pong needle position for a time and speed (cycles per second).
	static float NeedleAt(float Time, float Speed);

	// Guard detection gain per second for a player at Distance inside the guard's view.
	static float DetectionRate(float Distance, float Range, bool bCrouched, float Difficulty);

	// Combat: chance an enemy shot hits at Distance.
	static float EnemyHitChance(float Distance, bool bCrouched, float Difficulty);

	// "Mac_Overreacted" -> "Mac overreacted" (for consequence lines in menus).
	static FString PrettyFlag(FName Flag);
};
