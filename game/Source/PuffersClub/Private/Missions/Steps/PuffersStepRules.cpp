#include "Missions/Steps/PuffersStepRules.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bLower)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString S;
				if (Value.IsValid() && Value->TryGetString(S))
				{
					Out.Add(bLower ? S.ToLower() : S);
				}
			}
		}
		return Out;
	}

	void MergeInto(const TSharedRef<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Layer)
	{
		if (!Layer.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Layer->Values)
		{
			Target->SetField(Pair.Key, Pair.Value);
		}
	}

	bool IsWordChar(TCHAR C)
	{
		return FChar::IsAlnum(C) || C == TEXT('\'');
	}
}

bool FPuffersStepCatalog::LoadRulesFromString(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("StepRules is not a JSON object");
		return false;
	}

	ExcludedMissions.Reset();
	for (const FString& Id : ReadStrings(Root, TEXT("ExcludedMissions"), false))
	{
		ExcludedMissions.Add(*Id);
	}

	Defaults.Reset();
	const TSharedPtr<FJsonObject>* DefaultsObject = nullptr;
	if (Root->TryGetObjectField(TEXT("Defaults"), DefaultsObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*DefaultsObject)->Values)
		{
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Params))
			{
				Defaults.Add(Pair.Key.ToUpper(), *Params);
			}
		}
	}

	Rules.Reset();
	const TArray<TSharedPtr<FJsonValue>>* RuleValues = nullptr;
	if (Root->TryGetArrayField(TEXT("KeywordRules"), RuleValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RuleValues)
		{
			const TSharedPtr<FJsonObject>* RuleObject = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RuleObject))
			{
				continue;
			}
			FRule Rule;
			for (const FString& Type : ReadStrings(*RuleObject, TEXT("Types"), false))
			{
				Rule.Types.Add(Type.ToUpper());
			}
			Rule.Keywords = ReadStrings(*RuleObject, TEXT("Keywords"), true);
			for (const FString& Id : ReadStrings(*RuleObject, TEXT("Missions"), false))
			{
				Rule.Missions.Add(*Id);
			}
			bool bChoice = false;
			if ((*RuleObject)->TryGetBoolField(TEXT("Choice"), bChoice))
			{
				Rule.Choice = bChoice ? 1 : 0;
			}
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if ((*RuleObject)->TryGetObjectField(TEXT("Params"), Params))
			{
				Rule.Params = *Params;
			}
			Rules.Add(MoveTemp(Rule));
		}
	}

	Overrides.Reset();
	const TSharedPtr<FJsonObject>* OverridesObject = nullptr;
	if (Root->TryGetObjectField(TEXT("Overrides"), OverridesObject))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*OverridesObject)->Values)
		{
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Params))
			{
				Overrides.Add(*Pair.Key, *Params);
			}
		}
	}
	return true;
}

bool FPuffersStepCatalog::LoadReactionsFromString(const FString& Json, FString& OutError)
{
	Reactions.Reset();
	if (!FJsonObjectConverter::JsonArrayStringToUStruct(Json, &Reactions, 0, 0))
	{
		OutError = TEXT("Reactions is not a JSON array of reactions");
		return false;
	}
	return true;
}

FString FPuffersStepRules::TypeName(EPuffersObjectiveType Type)
{
	const UEnum* Enum = StaticEnum<EPuffersObjectiveType>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Type)) : FString();
}

bool FPuffersStepRules::MatchesKeyword(const FString& Text, const FString& Keyword)
{
	if (Keyword.IsEmpty())
	{
		return false;
	}
	const FString Lower = Text.ToLower();
	const FString Key = Keyword.ToLower();
	int32 From = 0;
	while (From <= Lower.Len() - Key.Len())
	{
		const int32 Found = Lower.Find(Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
		if (Found == INDEX_NONE)
		{
			return false;
		}
		if (Found == 0 || !IsWordChar(Lower[Found - 1]))
		{
			return true;
		}
		From = Found + 1;
	}
	return false;
}

int32 FPuffersStepRules::FindRule(const FPuffersStepCatalog& Catalog, FName MissionId, const FPuffersObjectiveDefinition& Objective)
{
	const FString Type = TypeName(Objective.Type);
	for (int32 Index = 0; Index < Catalog.Rules.Num(); ++Index)
	{
		const FPuffersStepCatalog::FRule& Rule = Catalog.Rules[Index];
		if (Rule.Types.Num() > 0 && !Rule.Types.Contains(Type))
		{
			continue;
		}
		if (Rule.Missions.Num() > 0 && !Rule.Missions.Contains(MissionId))
		{
			continue;
		}
		if (Rule.Choice >= 0 && (Rule.Choice == 1) == Objective.ChoiceGroup.IsNone())
		{
			continue;
		}
		if (Rule.Keywords.Num() == 0)
		{
			return Index;
		}
		for (const FString& Keyword : Rule.Keywords)
		{
			if (MatchesKeyword(Objective.Description, Keyword))
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

FPuffersStepParams FPuffersStepRules::Resolve(const FPuffersStepCatalog& Catalog, FName MissionId, const FPuffersObjectiveDefinition& Objective)
{
	const TSharedRef<FJsonObject> Merged = MakeShared<FJsonObject>();
	if (const TSharedPtr<FJsonObject>* Defaults = Catalog.Defaults.Find(TypeName(Objective.Type)))
	{
		MergeInto(Merged, *Defaults);
	}
	const int32 Rule = FindRule(Catalog, MissionId, Objective);
	if (Rule != INDEX_NONE)
	{
		MergeInto(Merged, Catalog.Rules[Rule].Params);
	}
	if (const TSharedPtr<FJsonObject>* Override = Catalog.Overrides.Find(Objective.Id))
	{
		MergeInto(Merged, *Override);
	}

	FPuffersStepParams Params;
	FJsonObjectConverter::JsonObjectToUStruct(Merged, &Params, 0, 0);
	if (!IsKnownGameplay(Params.Gameplay))
	{
		Params.Gameplay = TEXT("Prop");
	}
	return Params;
}

bool FPuffersStepRules::IsKnownGameplay(FName Gameplay)
{
	static const TSet<FName> Known = {
		TEXT("Reach"), TEXT("EnterVehicle"), TEXT("Route"), TEXT("Delivery"),
		TEXT("Hold"), TEXT("Timing"), TEXT("Mash"), TEXT("Rhythm"), TEXT("Memory"),
		TEXT("Conversation"), TEXT("Persuade"), TEXT("Negotiate"), TEXT("BusinessMenu"),
		TEXT("Tail"), TEXT("Escort"), TEXT("Chase"), TEXT("Escape"), TEXT("Stealth"), TEXT("Approach"),
		TEXT("Search"), TEXT("Photo"), TEXT("Video"), TEXT("Defend"), TEXT("Combat"),
		TEXT("Switch"), TEXT("Scan"), TEXT("LoseHeat"), TEXT("Prop") };
	return Known.Contains(Gameplay);
}

FName FPuffersStepRules::ParseCharacterPrefix(const FString& Description, const TArray<FName>& KnownCharacters)
{
	FString Head = Description;
	Head.TrimStartInline();
	if (Head.StartsWith(TEXT("As "), ESearchCase::IgnoreCase))
	{
		Head.RightChopInline(3);
		int32 Comma = INDEX_NONE;
		if (!Head.FindChar(TEXT(','), Comma))
		{
			return NAME_None;
		}
		Head.LeftInline(Comma);
	}
	else
	{
		int32 Colon = INDEX_NONE;
		if (!Head.FindChar(TEXT(':'), Colon) || Colon > 24)
		{
			return NAME_None;
		}
		Head.LeftInline(Colon);
	}
	Head.TrimStartAndEndInline();
	const FString Compact = Head.Replace(TEXT(" "), TEXT(""));
	for (const FName& Character : KnownCharacters)
	{
		if (Character.ToString().Equals(Compact, ESearchCase::IgnoreCase))
		{
			return Character;
		}
	}
	return NAME_None;
}

FName FPuffersStepRules::ParseSwitchTarget(const FString& Description, const TArray<FName>& KnownCharacters)
{
	const int32 At = Description.Find(TEXT("switch to "), ESearchCase::IgnoreCase);
	if (At == INDEX_NONE)
	{
		return FindNamedCharacter(Description, KnownCharacters);
	}
	return FindNamedCharacter(Description.RightChop(At + 10), KnownCharacters);
}

FName FPuffersStepRules::FindNamedCharacter(const FString& Description, const TArray<FName>& KnownCharacters)
{
	int32 BestAt = MAX_int32;
	FName Best = NAME_None;
	// Ids are compact ("CreamCake", "BigAl"); descriptions use spaces, so search a compacted copy,
	// but check word boundaries in the original text (otherwise the next word glues on: "BigAltells").
	FString Compact;
	TArray<int32> SourceIndex;
	Compact.Reserve(Description.Len());
	SourceIndex.Reserve(Description.Len());
	for (int32 I = 0; I < Description.Len(); ++I)
	{
		if (Description[I] != TEXT(' '))
		{
			Compact.AppendChar(Description[I]);
			SourceIndex.Add(I);
		}
	}
	for (const FName& Character : KnownCharacters)
	{
		const FString Name = Character.ToString();
		int32 At = Name.IsEmpty() ? INDEX_NONE : Compact.Find(Name, ESearchCase::IgnoreCase);
		while (At != INDEX_NONE)
		{
			const int32 First = SourceIndex[At];
			const int32 After = SourceIndex[At + Name.Len() - 1] + 1;
			const bool bStart = First == 0 || !FChar::IsAlpha(Description[First - 1]);
			const bool bEnd = After >= Description.Len() || !FChar::IsLower(Description[After]);
			if (bStart && bEnd)
			{
				break;
			}
			At = Compact.Find(Name, ESearchCase::IgnoreCase, ESearchDir::FromStart, At + 1);
		}
		if (At != INDEX_NONE && At < BestAt)
		{
			BestAt = At;
			Best = Character;
		}
	}
	return Best;
}

int32 FPuffersStepRules::StableIndex(FName Id, int32 Salt, int32 N)
{
	if (N <= 0)
	{
		return 0;
	}
	// FNV-1a over the lower-case id, so the result never changes between runs or platforms.
	uint32 Hash = 2166136261u ^ static_cast<uint32>(Salt);
	for (const TCHAR C : Id.ToString().ToLower())
	{
		Hash ^= static_cast<uint32>(C);
		Hash *= 16777619u;
	}
	return static_cast<int32>(Hash % static_cast<uint32>(N));
}

int32 FPuffersStepRules::PersuadeGain(int32 Tone, int32 Preferred, int32 Disliked)
{
	if (Tone == Preferred)
	{
		return 45;
	}
	if (Tone == Disliked)
	{
		return -10;
	}
	return 20;
}

FPuffersStepRules::EHaggle FPuffersStepRules::EvaluateOffer(float Offer, float Floor)
{
	if (Offer >= Floor)
	{
		return EHaggle::Deal;
	}
	return Offer >= Floor - 0.12f ? EHaggle::Warm : EHaggle::Cold;
}

float FPuffersStepRules::PhotoQuality(float AngleDegrees, float Distance, bool bViral)
{
	const float Centering = FMath::Clamp(1.f - AngleDegrees / 12.f, 0.f, 1.f);
	float Range = 1.f;
	if (Distance < 250.f)
	{
		Range = FMath::Clamp(Distance / 250.f, 0.f, 1.f);
	}
	else if (Distance > 1800.f)
	{
		Range = FMath::Clamp(1.f - (Distance - 1800.f) / 1700.f, 0.f, 1.f);
	}
	const float Quality = 0.6f * Centering + 0.4f * Range + (bViral ? 0.2f : 0.f);
	return Centering <= 0.f ? 0.f : FMath::Clamp(Quality, 0.f, 1.f);
}

bool FPuffersStepRules::IsNeedleInZone(float Needle, float ZoneCenter, float ZoneWidth)
{
	return FMath::Abs(Needle - ZoneCenter) <= ZoneWidth * 0.5f;
}

float FPuffersStepRules::NeedleAt(float Time, float Speed)
{
	const float Phase = FMath::Fmod(FMath::Max(0.f, Time) * Speed, 2.f);
	return Phase <= 1.f ? Phase : 2.f - Phase;
}

float FPuffersStepRules::DetectionRate(float Distance, float Range, bool bCrouched, float Difficulty)
{
	const float EffectiveRange = Range * (bCrouched ? 0.6f : 1.f);
	if (Distance >= EffectiveRange || EffectiveRange <= 0.f)
	{
		return 0.f;
	}
	const float Closeness = 1.f - Distance / EffectiveRange;
	// Full meter in about 3 s at the edge, well under a second up close.
	return (0.35f + 1.4f * Closeness) * FMath::Lerp(0.7f, 1.4f, FMath::Clamp(Difficulty, 0.f, 1.f));
}

float FPuffersStepRules::EnemyHitChance(float Distance, bool bCrouched, float Difficulty)
{
	const float Base = FMath::Clamp(0.55f - Distance / 4000.f, 0.08f, 0.55f);
	return Base * (bCrouched ? 0.5f : 1.f) * FMath::Lerp(0.7f, 1.3f, FMath::Clamp(Difficulty, 0.f, 1.f));
}

FString FPuffersStepRules::PrettyFlag(FName Flag)
{
	FString S = Flag.ToString().Replace(TEXT("_"), TEXT(" "));
	if (S.Len() > 1)
	{
		S = S.Left(1) + S.RightChop(1).ToLower();
	}
	return S;
}
