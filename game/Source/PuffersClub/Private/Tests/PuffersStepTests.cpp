// Step gameplay: how objectives resolve to gameplay, the mini-game scoring, and a data check that every
// objective in Content/Data/Missions plays as real gameplay rather than the stand-in prop.
// Run: Tools > Session Frontend > Automation, filter "Puffers.Steps".

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Missions/PuffersMissionTypes.h"
#include "Missions/Steps/PuffersStepRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PuffersStepTests
{
	const TCHAR* RulesJson = TEXT(R"JSON({
		"ExcludedMissions": ["BL01"],
		"Defaults": {
			"TALK": { "Gameplay": "Conversation" },
			"INTERACT": { "Gameplay": "Hold" },
			"BUSINESS_DECISION": { "Gameplay": "Negotiate" },
			"ESCAPE": { "Gameplay": "Escape", "StartMode": "Immediate" }
		},
		"KeywordRules": [
			{ "Types": ["INTERACT"], "Keywords": ["tackle"], "Params": { "Gameplay": "Chase" } },
			{ "Types": ["INTERACT", "BUSINESS_DECISION"], "Choice": true, "Params": { "Gameplay": "BusinessMenu" } },
			{ "Types": ["INTERACT"], "Keywords": ["chase"], "Params": { "Gameplay": "Chase" } },
			{ "Types": ["TALK"], "Keywords": ["convince"], "Missions": ["M01"], "Params": { "Gameplay": "Persuade", "Difficulty": 0.8 } },
			{ "Types": ["ESCAPE"], "Keywords": ["police"], "Params": { "Role": "Police", "Heat": 3 } },
			{ "Keywords": ["mystery"], "Params": { "Gameplay": "NotARealGame" } }
		],
		"Overrides": {
			"M01_Detain": { "Gameplay": "Approach", "Label": "Noticed" }
		}
	})JSON");

	FPuffersObjectiveDefinition Objective(const TCHAR* Id, EPuffersObjectiveType Type, const TCHAR* Description, FName Group = NAME_None)
	{
		FPuffersObjectiveDefinition O;
		O.Id = Id;
		O.Type = Type;
		O.Description = Description;
		O.ChoiceGroup = Group;
		return O;
	}

	FPuffersStepCatalog Catalog()
	{
		FPuffersStepCatalog C;
		FString Error;
		C.LoadRulesFromString(RulesJson, Error);
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepKeywords, "Puffers.Steps.Rules.KeywordsMatchWordStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepKeywords::RunTest(const FString&)
{
	TestTrue(TEXT("Start of text"), FPuffersStepRules::MatchesKeyword(TEXT("Chase him down"), TEXT("chase")));
	TestTrue(TEXT("Middle word, any case"), FPuffersStepRules::MatchesKeyword(TEXT("Give CHASE now"), TEXT("chase")));
	TestTrue(TEXT("Word start with a suffix"), FPuffersStepRules::MatchesKeyword(TEXT("He keeps chasing"), TEXT("chas")));
	TestFalse(TEXT("Not inside a word"), FPuffersStepRules::MatchesKeyword(TEXT("Make a purchase"), TEXT("chase")));
	TestTrue(TEXT("Phrases"), FPuffersStepRules::MatchesKeyword(TEXT("As Lato, talk your way past the mom"), TEXT("talk your way")));
	TestFalse(TEXT("Empty keyword never matches"), FPuffersStepRules::MatchesKeyword(TEXT("Anything"), TEXT("")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepResolve, "Puffers.Steps.Rules.ResolveLayersDefaultsRulesOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepResolve::RunTest(const FString&)
{
	using namespace PuffersStepTests;
	const FPuffersStepCatalog C = Catalog();
	TestTrue(TEXT("BL01 excluded"), C.ExcludedMissions.Contains(TEXT("BL01")));

	FPuffersStepParams P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("A"), EPuffersObjectiveType::TALK, TEXT("Say hi")));
	TestEqual(TEXT("Type default"), (P.Gameplay).ToString(), FString(TEXT("Conversation")));

	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("B"), EPuffersObjectiveType::TALK, TEXT("Convince the guard")));
	TestEqual(TEXT("Keyword rule"), (P.Gameplay).ToString(), FString(TEXT("Persuade")));
	TestEqual(TEXT("Rule params apply"), P.Difficulty, 0.8f);

	P = FPuffersStepRules::Resolve(C, TEXT("M02"), Objective(TEXT("C"), EPuffersObjectiveType::TALK, TEXT("Convince the guard")));
	TestEqual(TEXT("Mission-limited rule skips other missions"), (P.Gameplay).ToString(), FString(TEXT("Conversation")));

	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("M01_Detain"), EPuffersObjectiveType::INTERACT, TEXT("Stop him at the exit")));
	TestEqual(TEXT("Override wins"), (P.Gameplay).ToString(), FString(TEXT("Approach")));
	TestEqual(TEXT("Override params"), P.Label, FString(TEXT("Noticed")));

	P = FPuffersStepRules::Resolve(C, TEXT("H02"), Objective(TEXT("D"), EPuffersObjectiveType::ESCAPE, TEXT("Lose the police")));
	TestEqual(TEXT("Default kept under a params-only rule"), (P.Gameplay).ToString(), FString(TEXT("Escape")));
	TestEqual(TEXT("Role from rule"), (P.Role).ToString(), FString(TEXT("Police")));
	TestEqual(TEXT("Heat from rule"), P.Heat, 3);
	TestEqual(TEXT("Start mode from default"), P.StartMode.ToString(), FString(TEXT("Immediate")));

	P = FPuffersStepRules::Resolve(C, TEXT("H02"), Objective(TEXT("E"), EPuffersObjectiveType::INTERACT, TEXT("Open the mystery box")));
	TestEqual(TEXT("Unknown gameplay falls back to the prop"), (P.Gameplay).ToString(), FString(TEXT("Prop")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepChoiceRules, "Puffers.Steps.Rules.ChoiceOptionsBecomeMenus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepChoiceRules::RunTest(const FString&)
{
	using namespace PuffersStepTests;
	const FPuffersStepCatalog C = Catalog();
	FPuffersStepParams P = FPuffersStepRules::Resolve(C, TEXT("L01"), Objective(TEXT("A"), EPuffersObjectiveType::INTERACT, TEXT("Price it fair"), TEXT("Pricing")));
	TestEqual(TEXT("Choice option -> menu"), (P.Gameplay).ToString(), FString(TEXT("BusinessMenu")));
	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("B"), EPuffersObjectiveType::INTERACT, TEXT("Tackle the teen"), TEXT("Threat")));
	TestEqual(TEXT("Action rule above the choice rule keeps its gameplay"), (P.Gameplay).ToString(), FString(TEXT("Chase")));
	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("C"), EPuffersObjectiveType::INTERACT, TEXT("Give chase"), TEXT("Threat")));
	TestEqual(TEXT("Rules below the choice rule don't reach choice options"), (P.Gameplay).ToString(), FString(TEXT("BusinessMenu")));
	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("D"), EPuffersObjectiveType::INTERACT, TEXT("Price it fair")));
	TestEqual(TEXT("Non-choice objective skips the choice rule"), (P.Gameplay).ToString(), FString(TEXT("Hold")));
	P = FPuffersStepRules::Resolve(C, TEXT("M01"), Objective(TEXT("E"), EPuffersObjectiveType::BUSINESS_DECISION, TEXT("Negotiate price")));
	TestEqual(TEXT("Lone business decision -> negotiate"), (P.Gameplay).ToString(), FString(TEXT("Negotiate")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepCharacters, "Puffers.Steps.Rules.CharacterNamesInDescriptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepCharacters::RunTest(const FString&)
{
	const TArray<FName> Known = { TEXT("Haze"), TEXT("Lato"), TEXT("Mac"), TEXT("Kush"), TEXT("CreamCake"), TEXT("BigAl") };
	TestEqual(TEXT("Name: prefix"), (FPuffersStepRules::ParseCharacterPrefix(TEXT("Haze: get insulin to the senior center"), Known)).ToString(), FString(TEXT("Haze")));
	TestEqual(TEXT("As X, prefix"), (FPuffersStepRules::ParseCharacterPrefix(TEXT("As Kush, hold the crowd"), Known)).ToString(), FString(TEXT("Kush")));
	TestTrue(TEXT("Unknown prefix"), (FPuffersStepRules::ParseCharacterPrefix(TEXT("Tag: block party"), Known)).IsNone());
	TestEqual(TEXT("Switch target"), (FPuffersStepRules::ParseSwitchTarget(TEXT("Switch to Lato"), Known)).ToString(), FString(TEXT("Lato")));
	TestEqual(TEXT("Spaced name matches compact id"), (FPuffersStepRules::FindNamedCharacter(TEXT("Cream Cake swears he's followed"), Known)).ToString(), FString(TEXT("CreamCake")));
	TestTrue(TEXT("Name inside a longer word doesn't count"), (FPuffersStepRules::FindNamedCharacter(TEXT("Machines everywhere"), Known)).IsNone());
	TestEqual(TEXT("First named wins"), (FPuffersStepRules::FindNamedCharacter(TEXT("Big Al tells Haze"), Known)).ToString(), FString(TEXT("BigAl")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepScoring, "Puffers.Steps.Rules.MiniGameScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepScoring::RunTest(const FString&)
{
	const int32 A = FPuffersStepRules::StableIndex(TEXT("M01_Briefing"), 1, 3);
	TestEqual(TEXT("Stable pick repeats"), FPuffersStepRules::StableIndex(TEXT("m01_briefing"), 1, 3), A);
	TestTrue(TEXT("Stable pick in range"), A >= 0 && A < 3);
	TestEqual(TEXT("N of 0 is safe"), FPuffersStepRules::StableIndex(TEXT("X"), 0, 0), 0);

	TestEqual(TEXT("Preferred tone"), FPuffersStepRules::PersuadeGain(1, 1, 2), 45);
	TestEqual(TEXT("Disliked tone"), FPuffersStepRules::PersuadeGain(2, 1, 2), -10);
	TestEqual(TEXT("Neutral tone"), FPuffersStepRules::PersuadeGain(0, 1, 2), 20);

	TestTrue(TEXT("Offer at floor is a deal"), FPuffersStepRules::EvaluateOffer(0.8f, 0.8f) == FPuffersStepRules::EHaggle::Deal);
	TestTrue(TEXT("Close offer is warm"), FPuffersStepRules::EvaluateOffer(0.72f, 0.8f) == FPuffersStepRules::EHaggle::Warm);
	TestTrue(TEXT("Lowball is cold"), FPuffersStepRules::EvaluateOffer(0.5f, 0.8f) == FPuffersStepRules::EHaggle::Cold);

	TestTrue(TEXT("Centred, mid-range photo is great"), FPuffersStepRules::PhotoQuality(0.f, 800.f, false) > 0.95f);
	TestEqual(TEXT("Off-frame photo is worthless"), FPuffersStepRules::PhotoQuality(20.f, 800.f, false), 0.f);
	TestTrue(TEXT("Go Viral adds quality"), FPuffersStepRules::PhotoQuality(6.f, 800.f, true) > FPuffersStepRules::PhotoQuality(6.f, 800.f, false));
	TestTrue(TEXT("Too far is worse"), FPuffersStepRules::PhotoQuality(0.f, 3000.f, false) < FPuffersStepRules::PhotoQuality(0.f, 800.f, false));

	TestTrue(TEXT("Needle in zone"), FPuffersStepRules::IsNeedleInZone(0.52f, 0.5f, 0.1f));
	TestFalse(TEXT("Needle outside zone"), FPuffersStepRules::IsNeedleInZone(0.7f, 0.5f, 0.1f));
	TestEqual(TEXT("Needle ping-pongs back"), FPuffersStepRules::NeedleAt(1.5f, 1.f), 0.5f);
	TestEqual(TEXT("Needle at start"), FPuffersStepRules::NeedleAt(0.f, 1.f), 0.f);

	TestEqual(TEXT("Out of range: undetected"), FPuffersStepRules::DetectionRate(2000.f, 1500.f, false, 0.5f), 0.f);
	TestEqual(TEXT("Crouching shrinks the range"), FPuffersStepRules::DetectionRate(1000.f, 1500.f, true, 0.5f), 0.f);
	TestTrue(TEXT("Closer is faster"), FPuffersStepRules::DetectionRate(200.f, 1500.f, false, 0.5f) > FPuffersStepRules::DetectionRate(1200.f, 1500.f, false, 0.5f));

	TestTrue(TEXT("Crouched is harder to hit"), FPuffersStepRules::EnemyHitChance(500.f, true, 0.5f) < FPuffersStepRules::EnemyHitChance(500.f, false, 0.5f));
	TestEqual(TEXT("Pretty flag"), FPuffersStepRules::PrettyFlag(TEXT("Mac_Overreacted")), FString(TEXT("Mac overreacted")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersStepEveryObjective, "Puffers.Steps.Data.EveryObjectiveHasGameplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuffersStepEveryObjective::RunTest(const FString&)
{
	const FString StepDir = FPaths::ProjectContentDir() / TEXT("Data/Steps");
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *(StepDir / TEXT("StepRules.json"))))
	{
		AddError(TEXT("Content/Data/Steps/StepRules.json is missing"));
		return false;
	}
	FPuffersStepCatalog Catalog;
	FString Error;
	if (!Catalog.LoadRulesFromString(Json, Error))
	{
		AddError(Error);
		return false;
	}
	if (FFileHelper::LoadFileToString(Json, *(StepDir / TEXT("Reactions.json"))) && !Catalog.LoadReactionsFromString(Json, Error))
	{
		AddError(Error);
	}

	const FString MissionDir = FPaths::ProjectContentDir() / TEXT("Data/Missions");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(MissionDir / TEXT("*.json")), true, false);
	TestTrue(TEXT("Mission files found"), Files.Num() > 0);

	TSet<FName> MissionIds;
	int32 Checked = 0;
	for (const FString& File : Files)
	{
		TArray<FPuffersMissionDefinition> Missions;
		if (!FFileHelper::LoadFileToString(Json, *(MissionDir / File)) || !FJsonObjectConverter::JsonArrayStringToUStruct(Json, &Missions, 0, 0))
		{
			AddError(FString::Printf(TEXT("%s did not parse"), *File));
			continue;
		}
		for (const FPuffersMissionDefinition& Mission : Missions)
		{
			MissionIds.Add(Mission.Id);
			if (Catalog.ExcludedMissions.Contains(Mission.Id))
			{
				continue;
			}
			for (const FPuffersObjectiveDefinition& Objective : Mission.Objectives)
			{
				++Checked;
				const FPuffersStepParams Params = FPuffersStepRules::Resolve(Catalog, Mission.Id, Objective);
				if (Params.Gameplay == TEXT("Prop"))
				{
					AddError(FString::Printf(TEXT("%s / %s (%s) has no gameplay: \"%s\""), *Mission.Id.ToString(),
						*Objective.Id.ToString(), *FPuffersStepRules::TypeName(Objective.Type), *Objective.Description));
				}
			}
		}
	}
	for (const FPuffersStepReaction& Reaction : Catalog.Reactions)
	{
		for (const FName& Mission : Reaction.Missions)
		{
			TestTrue(FString::Printf(TEXT("Reaction for %s names a real mission (%s)"), *Reaction.Flag.ToString(), *Mission.ToString()), MissionIds.Contains(Mission));
		}
	}
	AddInfo(FString::Printf(TEXT("Checked %d objectives in %d missions"), Checked, MissionIds.Num()));
	return true;
}

#endif
