// Core of the career subsystem: setup, data, wallet, free-roam work, route and licensing, operations, standing,
// the first-build checklist and data-driven hooks. Law and insiders, care/raids/housing and the workforce live in
// PuffersCareerLaw.cpp, PuffersCareerEstate.cpp and PuffersCareerWorkforce.cpp.

#include "Career/PuffersCareerSubsystem.h"
#include "Career/PuffersCareerRules.h"
#include "Career/PuffersCountyLawRules.h"
#include "Missions/PuffersMissionSubsystem.h"
#include "Law/PuffersLawSubsystem.h"
#include "Law/PuffersSentenceSubsystem.h"
#include "Economy/PuffersInventorySubsystem.h"
#include "PuffersClub.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<int32> CVarCareerLocalAuthority(
	TEXT("puffers.Career.LocalAuthority"),
#if UE_BUILD_SHIPPING
	0,
#else
	1,
#endif
	TEXT("1 = a standalone game acts as its own server for the shared-world economy (Test Yard playtests). ")
	TEXT("0 = the economy, housing, care, raids, payroll and loans need an online world. Story missions work either way."),
	ECVF_Default);

const FString UPuffersCareerSubsystem::SaveSlotName = TEXT("PuffersCareer0");
const FName UPuffersCareerSubsystem::EventRouteCommitted = TEXT("CareerRouteCommitted");
const FName UPuffersCareerSubsystem::EventLicenseGranted = TEXT("CareerLicenseGranted");
const FName UPuffersCareerSubsystem::EventLicensesRevoked = TEXT("CareerLicensesRevoked");
const FName UPuffersCareerSubsystem::EventOperationOpened = TEXT("CareerOperationOpened");
const FName UPuffersCareerSubsystem::EventLawPublished = TEXT("CountyLawPublished");
const FName UPuffersCareerSubsystem::EventCountySentence = TEXT("CountySentenceImposed");
const FName UPuffersCareerSubsystem::EventRaidSettled = TEXT("EstateRaidSettled");
const FName UPuffersCareerSubsystem::EventPayrollUnpaid = TEXT("PayrollUnpaid");
const FName UPuffersCareerSubsystem::EventRobTheBossOffered = TEXT("RobTheBossOffered");
const FName UPuffersCareerSubsystem::EventInsiderFallout = TEXT("InsiderFallout");

namespace PuffersCareer
{
	template <typename T>
	bool LoadObjectFile(const FString& RelativePath, T& Out)
	{
		FString Json;
		const FString Path = FPaths::ProjectContentDir() / RelativePath;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			UE_LOG(LogPuffers, Warning, TEXT("Career data missing: %s"), *Path);
			return false;
		}
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Out, 0, 0))
		{
			UE_LOG(LogPuffers, Error, TEXT("Career data failed to parse: %s"), *Path);
			return false;
		}
		return true;
	}

	template <typename TEnum>
	bool EnumFromString(const FString& Text, TEnum& Out)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		const int64 Value = Enum ? Enum->GetValueByNameString(Text) : INDEX_NONE;
		if (Value == INDEX_NONE)
		{
			return false;
		}
		Out = static_cast<TEnum>(Value);
		return true;
	}

	template <typename TEnum>
	FString EnumToString(TEnum Value)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : TEXT("?");
	}
}

// ---------------------------------------------------------------------------------------------------------- Setup

void UPuffersCareerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UPuffersMissionSubsystem>();
	Collection.InitializeDependency<UPuffersLawSubsystem>();
	Collection.InitializeDependency<UPuffersSentenceSubsystem>();
	Collection.InitializeDependency<UPuffersInventorySubsystem>();
	Super::Initialize(Collection);

	UGameInstance* GI = GetGameInstance();
	GetWalletCash = [GI]() -> int64
	{
		const UPuffersMissionSubsystem* Missions = GI->GetSubsystem<UPuffersMissionSubsystem>();
		return Missions ? Missions->GetPuffCash() : 0;
	};
	AddWalletCash = [GI](int64 Amount)
	{
		if (UPuffersMissionSubsystem* Missions = GI->GetSubsystem<UPuffersMissionSubsystem>())
		{
			Missions->AddPuffCash(static_cast<int32>(FMath::Clamp<int64>(Amount, MIN_int32, MAX_int32)));
			Missions->SaveNow();
		}
	};
	Broadcast = [GI](FName EventName, FName Context)
	{
		if (UPuffersMissionSubsystem* Missions = GI->GetSubsystem<UPuffersMissionSubsystem>())
		{
			Missions->BroadcastGameEvent(EventName, Context);
		}
	};
	FailActiveMission = [GI](FName Reason)
	{
		if (UPuffersMissionSubsystem* Missions = GI->GetSubsystem<UPuffersMissionSubsystem>())
		{
			Missions->FailActiveMission(Reason);
		}
	};
	ImposeCustody = [GI](const FPuffersSentenceDefinition& Definition) -> bool
	{
		UPuffersSentenceSubsystem* Sentences = GI->GetSubsystem<UPuffersSentenceSubsystem>();
		if (!Sentences)
		{
			return false;
		}
		Sentences->RegisterGeneratedDefinition(Definition);
		return Sentences->ImposeSentence(Definition.SentenceId);
	};
	MapCountyAt = [GI](const FVector& Location) -> FName
	{
		const UPuffersLawSubsystem* Law = GI->GetSubsystem<UPuffersLawSubsystem>();
		return Law ? Law->GetCountyAt(Location) : NAME_None;
	};
	GetInventory = [GI]() -> TMap<FName, int32>
	{
		const UPuffersInventorySubsystem* Inventory = GI->GetSubsystem<UPuffersInventorySubsystem>();
		return Inventory ? Inventory->GetAll() : TMap<FName, int32>();
	};
	RemoveInventory = [GI](FName ItemId, int32 Count) -> bool
	{
		UPuffersInventorySubsystem* Inventory = GI->GetSubsystem<UPuffersInventorySubsystem>();
		return Inventory && Inventory->Remove(ItemId, Count);
	};
	AddInventory = [GI](FName ItemId, int32 Count)
	{
		if (UPuffersInventorySubsystem* Inventory = GI->GetSubsystem<UPuffersInventorySubsystem>())
		{
			Inventory->Add(ItemId, Count);
		}
	};

	ReloadData();
	LoadOrCreate();
	ReRegisterCountySentences();

	if (UPuffersMissionSubsystem* Missions = GI->GetSubsystem<UPuffersMissionSubsystem>())
	{
		Missions->OnGameEvent.AddDynamic(this, &UPuffersCareerSubsystem::OnGameEvent);
		Missions->OnObjectiveUpdated.AddDynamic(this, &UPuffersCareerSubsystem::OnObjectiveUpdated);
	}
}

void UPuffersCareerSubsystem::Deinitialize()
{
	SaveNow();
	Super::Deinitialize();
}

void UPuffersCareerSubsystem::InitializeForTests(const FPuffersCareerConfig& InConfig, const FPuffersCountyLawCatalog& InLaw,
	const FPuffersInsiderCatalog& InInsiders, const FPuffersHousingCatalog& InHousing, const FPuffersCareerHookCatalog& InHooks)
{
	bForTests = true;
	Config = InConfig;
	LawCatalog = InLaw;
	InsiderCatalog = InInsiders;
	HousingCatalog = InHousing;
	HookCatalog = InHooks;
	Save = NewObject<UPuffersCareerSaveGame>(this);
	Save->Owner.CareerOwnerId = CareerOwnerId;
	FPuffersCountyLawRules::SeedCounties(Save->Law, LawCatalog.Counties);
	SharedWorldOverride = []() { return true; };
}

int32 UPuffersCareerSubsystem::ReloadData()
{
	int32 Loaded = 0;
	Loaded += PuffersCareer::LoadObjectFile(TEXT("Data/Career/Career.json"), Config) ? 1 : 0;
	Loaded += PuffersCareer::LoadObjectFile(TEXT("Data/Career/CountyLaws.json"), LawCatalog) ? 1 : 0;
	Loaded += PuffersCareer::LoadObjectFile(TEXT("Data/Career/Insiders.json"), InsiderCatalog) ? 1 : 0;
	Loaded += PuffersCareer::LoadObjectFile(TEXT("Data/Career/Housing.json"), HousingCatalog) ? 1 : 0;
	Loaded += PuffersCareer::LoadObjectFile(TEXT("Data/Career/Hooks.json"), HookCatalog) ? 1 : 0;
	UE_LOG(LogPuffers, Log, TEXT("Career data: %d of 5 files, %d archetypes, %d counties, %d insiders, %d hooks"),
		Loaded, Config.Archetypes.Num(), LawCatalog.Counties.Num(), InsiderCatalog.Insiders.Num(), HookCatalog.Hooks.Num());
	if (Save)
	{
		FPuffersCountyLawRules::SeedCounties(Save->Law, LawCatalog.Counties);
	}
	return Loaded;
}

void UPuffersCareerSubsystem::LoadOrCreate()
{
	Save = Cast<UPuffersCareerSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!Save)
	{
		Save = Cast<UPuffersCareerSaveGame>(UGameplayStatics::CreateSaveGameObject(UPuffersCareerSaveGame::StaticClass()));
	}
	if (Save->Owner.CareerOwnerId.IsNone())
	{
		Save->Owner.CareerOwnerId = CareerOwnerId;
	}
	FPuffersCountyLawRules::SeedCounties(Save->Law, LawCatalog.Counties);
	if (!Save->Milestones.Contains(EPuffersFirstBuildMilestone::LawSeeded) && Save->Law.Versions.Num() > 0)
	{
		Save->Milestones.Add(EPuffersFirstBuildMilestone::LawSeeded);
	}
}

bool UPuffersCareerSubsystem::SaveNow()
{
	if (!Save || bForTests)
	{
		return Save != nullptr;
	}
	return UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
}

void UPuffersCareerSubsystem::ReRegisterCountySentences()
{
	if (!Save || bForTests)
	{
		return;
	}
	if (UPuffersSentenceSubsystem* Sentences = GetGameInstance()->GetSubsystem<UPuffersSentenceSubsystem>())
	{
		for (const FPuffersCountySentence& Sentence : Save->Law.Sentences)
		{
			Sentences->RegisterGeneratedDefinition(Sentence.Definition);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------- Tick

TStatId UPuffersCareerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPuffersCareerSubsystem, STATGROUP_Tickables);
}

bool UPuffersCareerSubsystem::IsTickable() const
{
	return Save != nullptr && !bForTests && !IsTemplate();
}

void UPuffersCareerSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->IsPaused())
	{
		return;
	}

	// Active time per phase: only while a player pawn exists and the game isn't paused (menus pause).
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			if (Pawn->GetVelocity().SizeSquared() > 1.f || PC->IsInputKeyDown(EKeys::AnyKey))
			{
				const FName Phase = Save->Standing.PhaseId.IsNone() ? FName(TEXT("Uncommitted")) : Save->Standing.PhaseId;
				Save->Standing.ActiveSecondsByPhase.FindOrAdd(Phase) += FMath::Min(DeltaTime, 0.25f);
			}
		}
	}

	SecondsSinceHousekeeping += DeltaTime;
	if (SecondsSinceHousekeeping >= 5.f)
	{
		SecondsSinceHousekeeping = 0.f;
		RunHousekeeping();
	}

	SecondsSinceSave += DeltaTime;
	if (SecondsSinceSave >= 30.f)
	{
		SecondsSinceSave = 0.f;
		SaveNow();
	}
}

void UPuffersCareerSubsystem::RunHousekeeping()
{
	if (!Save)
	{
		return;
	}
	PublishDueLaws();
	if (IsSharedWorld() && HasAuthority())
	{
		AdvanceHousing();
		AdvanceLoans();
		CollectLeaseRent();
		WeeklyRollover();
	}
}

void UPuffersCareerSubsystem::WeeklyRollover()
{
	const FDateTime Current = Now();
	if (Save->LastWeekRollover.GetTicks() == 0)
	{
		Save->LastWeekRollover = Current;
		for (const FPuffersOperation& Operation : Save->Owner.Operations)
		{
			Save->ProfitAtLastWeek.Add(Operation.OperationId, Operation.NetProfit);
		}
		return;
	}
	const FTimespan Week = FTimespan::FromDays(FMath::Max(1, Config.PayrollDays));
	while (Current - Save->LastWeekRollover >= Week)
	{
		Save->LastWeekRollover += Week;
		for (FPuffersOperation& Operation : Save->Owner.Operations)
		{
			const int64 Before = Save->ProfitAtLastWeek.FindRef(Operation.OperationId);
			Operation.ProfitableWeeks = (Operation.bOpen && Operation.NetProfit > Before) ? Operation.ProfitableWeeks + 1 : 0;
			Save->ProfitAtLastWeek.Add(Operation.OperationId, Operation.NetProfit);
		}
		// Pay day for every business with staff.
		TSet<FName> Businesses;
		for (const FPuffersEmployment& E : Save->Workforce.Employment)
		{
			Businesses.Add(E.BusinessId);
		}
		for (const FName& Business : Businesses)
		{
			RunPayroll(Business, Save->LastWeekRollover);
		}
	}
	RecomputeStanding();
}

// ---------------------------------------------------------------------------------------------------------- Modes

bool UPuffersCareerSubsystem::IsSharedWorld() const
{
	if (SharedWorldOverride)
	{
		return SharedWorldOverride();
	}
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && World->GetNetMode() != NM_Standalone)
	{
		return true;
	}
	return CVarCareerLocalAuthority.GetValueOnGameThread() != 0;
}

bool UPuffersCareerSubsystem::HasAuthority() const
{
	if (bForTests)
	{
		return true;
	}
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return !World || World->GetNetMode() != NM_Client;
}

bool UPuffersCareerSubsystem::RequireShared(FString& OutWhy) const
{
	if (!IsSharedWorld())
	{
		OutWhy = TEXT("This is part of the online world. Offline play covers story missions only.");
		return false;
	}
	if (!HasAuthority())
	{
		OutWhy = TEXT("The server handles this.");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Helpers

FName UPuffersCareerSubsystem::MakeId(const TCHAR* Prefix)
{
	return FName(*FString::Printf(TEXT("%s_%d"), Prefix, Save->NextSerial++));
}

void UPuffersCareerSubsystem::Notify(const FString& Text)
{
	Save->Notices.Add(Text);
	while (Save->Notices.Num() > 50)
	{
		Save->Notices.RemoveAt(0);
	}
	UE_LOG(LogPuffers, Log, TEXT("[Career] %s"), *Text);
	OnNotice.Broadcast(Text);
}

void UPuffersCareerSubsystem::Publish(FName EventName, FName Context)
{
	OnCareerEvent.Broadcast(EventName, Context);
	if (Broadcast)
	{
		Broadcast(EventName, Context);
	}
}

void UPuffersCareerSubsystem::Milestone(EPuffersFirstBuildMilestone Which)
{
	if (!Save->Milestones.Contains(Which))
	{
		Save->Milestones.Add(Which);
		Notify(FString::Printf(TEXT("First operation milestone: %s"), *PuffersCareer::EnumToString(Which)));
	}
}

bool UPuffersCareerSubsystem::HasMilestone(EPuffersFirstBuildMilestone Which) const
{
	return Save && Save->Milestones.Contains(Which);
}

TArray<FString> UPuffersCareerSubsystem::GetFirstBuildChecklist() const
{
	TArray<FString> Lines;
	const UEnum* Enum = StaticEnum<EPuffersFirstBuildMilestone>();
	for (int32 Index = 0; Enum && Index < Enum->NumEnums() - 1; ++Index)
	{
		const EPuffersFirstBuildMilestone Value = static_cast<EPuffersFirstBuildMilestone>(Enum->GetValueByIndex(Index));
		Lines.Add(FString::Printf(TEXT("%s %s"), HasMilestone(Value) ? TEXT("[x]") : TEXT("[ ]"), *Enum->GetNameStringByIndex(Index)));
	}
	return Lines;
}

TArray<FString> UPuffersCareerSubsystem::GetNotices(int32 Count) const
{
	TArray<FString> Out;
	if (Save)
	{
		for (int32 Index = FMath::Max(0, Save->Notices.Num() - Count); Index < Save->Notices.Num(); ++Index)
		{
			Out.Add(Save->Notices[Index]);
		}
	}
	return Out;
}

const FPuffersBusinessArchetype* UPuffersCareerSubsystem::FindArchetype(FName Id) const
{
	return Config.Archetypes.FindByPredicate([Id](const FPuffersBusinessArchetype& A) { return A.Id == Id; });
}

// ---------------------------------------------------------------------------------------------------------- Wallet

void UPuffersCareerSubsystem::SyncPocket()
{
	if (!Save || !GetWalletCash)
	{
		return;
	}
	const int64 Wallet = GetWalletCash();
	const int64 Pocket = Save->Ledger.GetBalance(FPuffersLedger::Pocket);
	const int64 Delta = Wallet - Pocket;
	if (Delta == 0)
	{
		return;
	}
	const FName TxId = MakeId(TEXT("SYNC"));
	const EPuffersLedgerResult Result = Delta > 0
		? Save->Ledger.Transfer(TxId, TEXT("World.Untracked"), FPuffersLedger::Pocket, Delta, TEXT("Wallet activity"), Now())
		: Save->Ledger.Transfer(TxId, FPuffersLedger::Pocket, TEXT("World.Untracked"), -Delta, TEXT("Wallet activity"), Now());
	if (Result != EPuffersLedgerResult::Ok)
	{
		UE_LOG(LogPuffers, Warning, TEXT("Career wallet sync of %lld failed (%s)"), Delta, *PuffersCareer::EnumToString(Result));
	}
}

void UPuffersCareerSubsystem::MirrorPocketDelta(int64 PocketBefore)
{
	const int64 Delta = Save->Ledger.GetBalance(FPuffersLedger::Pocket) - PocketBefore;
	if (Delta != 0 && AddWalletCash)
	{
		AddWalletCash(Delta);
	}
}

EPuffersLedgerResult UPuffersCareerSubsystem::PostLedger(TFunctionRef<EPuffersLedgerResult(FPuffersLedger&)> Op)
{
	if (!Save)
	{
		return EPuffersLedgerResult::Invalid;
	}
	SyncPocket();
	const int64 Before = Save->Ledger.GetBalance(FPuffersLedger::Pocket);
	const EPuffersLedgerResult Result = Op(Save->Ledger);
	if (Result == EPuffersLedgerResult::Ok)
	{
		MirrorPocketDelta(Before);
	}
	return Result;
}

int64 UPuffersCareerSubsystem::GetBalance(FName Account) const
{
	if (Account == FPuffersLedger::Pocket && GetWalletCash)
	{
		return GetWalletCash();
	}
	return Save ? Save->Ledger.GetBalance(Account) : 0;
}

int64 UPuffersCareerSubsystem::GetAvailable(FName Account) const
{
	if (!Save)
	{
		return 0;
	}
	return GetBalance(Account) - Save->Ledger.GetHeld(Account);
}

EPuffersLedgerResult UPuffersCareerSubsystem::TransferBetweenAccounts(FName TxId, FName From, FName To, int64 Amount, const FString& Memo)
{
	if (!FPuffersLedger::IsOwnedAccount(From) || !FPuffersLedger::IsOwnedAccount(To))
	{
		return EPuffersLedgerResult::Invalid;
	}
	const FDateTime At = Now();
	return PostLedger([&](FPuffersLedger& L) { return L.Transfer(TxId, From, To, Amount, Memo, At); });
}

EPuffersLedgerResult UPuffersCareerSubsystem::PostWorld(FName TxId, FName Account, int64 Amount, FName WorldAccount, const FString& Memo)
{
	if (!FPuffersLedger::IsOwnedAccount(Account) || FPuffersLedger::IsOwnedAccount(WorldAccount) || Amount == 0)
	{
		return EPuffersLedgerResult::Invalid;
	}
	const FDateTime At = Now();
	return PostLedger([&](FPuffersLedger& L)
	{
		return Amount > 0 ? L.Transfer(TxId, WorldAccount, Account, Amount, Memo, At) : L.Transfer(TxId, Account, WorldAccount, -Amount, Memo, At);
	});
}

EPuffersLedgerResult UPuffersCareerSubsystem::PlaceHold(FName HoldId, FName Account, int64 Amount, const FString& Memo)
{
	if (!Save)
	{
		return EPuffersLedgerResult::Invalid;
	}
	SyncPocket();
	return Save->Ledger.PlaceHold(HoldId, Account, Amount, Memo, Now());
}

EPuffersLedgerResult UPuffersCareerSubsystem::CaptureHold(FName HoldId, FName TxId, FName To, int64 Amount)
{
	const FDateTime At = Now();
	return PostLedger([&](FPuffersLedger& L) { return L.CaptureHold(HoldId, TxId, To, Amount, At); });
}

EPuffersLedgerResult UPuffersCareerSubsystem::ReleaseHold(FName HoldId)
{
	return Save ? Save->Ledger.ReleaseHold(HoldId, Now()) : EPuffersLedgerResult::Invalid;
}

EPuffersLedgerResult UPuffersCareerSubsystem::ReverseTransaction(FName TxId, FName NewTxId, const FString& Memo)
{
	const FDateTime At = Now();
	return PostLedger([&](FPuffersLedger& L) { return L.Reverse(TxId, NewTxId, Memo, At); });
}

FPuffersReceipt UPuffersCareerSubsystem::GetReceipt(FName TxId) const
{
	return Save ? Save->Ledger.MakeReceipt(TxId) : FPuffersReceipt();
}

TArray<FPuffersLedgerTransaction> UPuffersCareerSubsystem::GetRecentTransactions(int32 Count) const
{
	TArray<FPuffersLedgerTransaction> Out;
	if (Save)
	{
		const TArray<FPuffersLedgerTransaction>& All = Save->Ledger.Transactions;
		for (int32 Index = FMath::Max(0, All.Num() - Count); Index < All.Num(); ++Index)
		{
			Out.Add(All[Index]);
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------------------- Work

bool UPuffersCareerSubsystem::PayShift(FName OpId, int32 Wage, FName JobId)
{
	if (!Save || Wage <= 0)
	{
		return false;
	}
	const FName TxId = FName(*FString::Printf(TEXT("SHIFT_%s"), *OpId.ToString()));
	const EPuffersLedgerResult Result = PostWorld(TxId, FPuffersLedger::Pocket, Wage, TEXT("World.Wages"),
		FString::Printf(TEXT("Shift pay (%s)"), *JobId.ToString()));
	if (Result != EPuffersLedgerResult::Ok)
	{
		return false;
	}
	Save->Standing.LifetimeWages += Wage;
	Notify(FString::Printf(TEXT("Shift paid: %d Puff Cash. Savings %lld. Licensed shop needs %d, street needs %d."),
		Wage, GetBalance(FPuffersLedger::Pocket), Config.LicensedCapitalTarget, Config.StreetCapitalTarget));
	Milestone(EPuffersFirstBuildMilestone::PaidShift);
	if (Save->Estate.EstateResets > 0 || Save->Owner.bLicensesForfeited)
	{
		Milestone(EPuffersFirstBuildMilestone::RecoveryJob);
	}

	// A paid shift is a care activity for online careers that clocked in.
	if (Save->Estate.bClockedIn)
	{
		FString Why;
		CompleteCareActivity(FName(*FString::Printf(TEXT("CARE_%s"), *TxId.ToString())), TEXT("PaidShift"), Why);
	}
	RecomputeStanding();
	SaveNow();
	return true;
}

// ---------------------------------------------------------------------------------------------------------- Route

EPuffersCareerRoute UPuffersCareerSubsystem::GetRoute() const
{
	return Save ? FPuffersCareerRules::GetRoute(Save->Owner) : EPuffersCareerRoute::Uncommitted;
}

FString UPuffersCareerSubsystem::GetCommitmentText(EPuffersOperationKind Kind) const
{
	return FPuffersCareerRules::DescribeCommitment(GetRoute(), Kind);
}

bool UPuffersCareerSubsystem::CanHoldCannabisLicense(FString& OutReason) const
{
	return Save && FPuffersCareerRules::CanHoldCannabisLicense(Save->Owner, OutReason);
}

FName UPuffersCareerSubsystem::StartOperation(FName OpId, FName ArchetypeId, FName CountyId, FName PremisesId, const FString& DisplayName, FString& OutReason)
{
	if (!Save)
	{
		return NAME_None;
	}
	if (!RequireShared(OutReason))
	{
		return NAME_None;
	}
	const FPuffersBusinessArchetype* Archetype = FindArchetype(ArchetypeId);
	if (!Archetype)
	{
		OutReason = FString::Printf(TEXT("Unknown business type %s."), *ArchetypeId.ToString());
		return NAME_None;
	}
	const FName OperationId = OpId.IsNone() ? MakeId(TEXT("OP")) : OpId;
	if (FPuffersCareerRules::FindOperation(Save->Owner, OperationId))
	{
		OutReason = TEXT("Already started.");
		return OperationId;
	}
	if (Archetype->Kind == EPuffersOperationKind::LicensedCannabis && !FPuffersCareerRules::CanHoldCannabisLicense(Save->Owner, OutReason))
	{
		return NAME_None;
	}
	if (CountyId.IsNone())
	{
		CountyId = LawCatalog.DefaultCounty;
	}

	// Pay the entry cost once (the op id makes retries safe).
	if (Archetype->EntryCost > 0)
	{
		const FName TxId = FName(*FString::Printf(TEXT("ENTRY_%s"), *OperationId.ToString()));
		const EPuffersLedgerResult Paid = PostWorld(TxId, FPuffersLedger::Pocket, -Archetype->EntryCost, TEXT("World.Market"),
			FString::Printf(TEXT("Opening %s"), *Archetype->Name));
		if (Paid != EPuffersLedgerResult::Ok && Paid != EPuffersLedgerResult::Duplicate)
		{
			OutReason = FString::Printf(TEXT("You need %d Puff Cash to open a %s."), Archetype->EntryCost, *Archetype->Name);
			return NAME_None;
		}
	}

	FPuffersOperation& Operation = Save->Owner.Operations.AddDefaulted_GetRef();
	Operation.OperationId = OperationId;
	Operation.ArchetypeId = ArchetypeId;
	Operation.Kind = Archetype->Kind;
	Operation.DisplayName = DisplayName.IsEmpty() ? Archetype->Name : DisplayName;
	Operation.CountyId = CountyId;
	Operation.PremisesId = PremisesId;
	Operation.OpenedAt = Now();

	if (Archetype->Kind == EPuffersOperationKind::Street)
	{
		if (FPuffersCareerRules::CommitStreet(Save->Owner, Now()))
		{
			Publish(EventRouteCommitted, TEXT("Street"));
			Notify(TEXT("You're committed to the street for this career. Licensed shops stay possible until a confirmed street bust."));
			if (Save->Owner.Licenses.ContainsByPredicate([](const FPuffersLicense& L) { return L.State == EPuffersLicenseState::Active; }))
			{
				Milestone(EPuffersFirstBuildMilestone::DualOperation);
			}
		}
		Milestone(EPuffersFirstBuildMilestone::StreetOwnership);
	}
	else if (Archetype->Kind == EPuffersOperationKind::LicensedCannabis)
	{
		const FName LicenseId = FName(*FString::Printf(TEXT("LIC_%s"), *OperationId.ToString()));
		FString Why;
		if (FPuffersCareerRules::GrantLicense(Save->Owner, LicenseId, OperationId, CountyId, Now(), Why))
		{
			Publish(EventLicenseGranted, LicenseId);
			Milestone(EPuffersFirstBuildMilestone::LicensedOwnership);
			if (Save->Owner.bStreetCommitted)
			{
				Milestone(EPuffersFirstBuildMilestone::DualOperation);
			}
		}
	}

	Notify(FString::Printf(TEXT("%s started in %s. %s"), *Operation.DisplayName, *CountyId.ToString(), *FPuffersCareerRules::DescribeCommitment(GetRoute(), Archetype->Kind)));
	RecomputeStanding();
	SaveNow();
	return OperationId;
}

bool UPuffersCareerSubsystem::InstallFixture(FName OperationId, FName FixtureId)
{
	FPuffersOperation* Operation = Save ? FPuffersCareerRules::FindOperation(Save->Owner, OperationId) : nullptr;
	if (!Operation || Operation->InstalledFixtures.Contains(FixtureId))
	{
		return false;
	}
	Operation->InstalledFixtures.Add(FixtureId);
	return true;
}

bool UPuffersCareerSubsystem::OpenOperation(FName OperationId, FString& OutReason)
{
	if (!Save || !RequireShared(OutReason))
	{
		return false;
	}
	FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, OperationId);
	const FPuffersBusinessArchetype* Archetype = Operation ? FindArchetype(Operation->ArchetypeId) : nullptr;
	if (!Operation || !Archetype)
	{
		OutReason = TEXT("No such operation.");
		return false;
	}
	if (Operation->bOpen)
	{
		return true;
	}
	// The owner works the counter, so a one-person shop can open on day one.
	int32 Staff = 1;
	for (const FPuffersEmployment& E : Save->Workforce.Employment)
	{
		Staff += (E.BusinessId == OperationId && E.State == EPuffersEmploymentState::Active) ? 1 : 0;
	}
	if (!FPuffersCareerRules::CanOpenOperation(Save->Owner, *Operation, *Archetype, Staff, OutReason))
	{
		return false;
	}
	Operation->bOpen = true;
	Publish(EventOperationOpened, OperationId);
	Notify(FString::Printf(TEXT("%s is open for business."), *Operation->DisplayName));
	RecomputeStanding();
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::CloseOperation(FName OperationId)
{
	FPuffersOperation* Operation = Save ? FPuffersCareerRules::FindOperation(Save->Owner, OperationId) : nullptr;
	if (!Operation || !Operation->bOpen)
	{
		return false;
	}
	Operation->bOpen = false;
	// Closing a street operation never undoes the street commitment.
	Notify(FString::Printf(TEXT("%s closed. Wages already earned are still owed."), *Operation->DisplayName));
	RecomputeStanding();
	SaveNow();
	return true;
}

bool UPuffersCareerSubsystem::RecordSale(FName TxId, FName OperationId, int32 Amount, FString& OutReason)
{
	if (!Save || Amount <= 0)
	{
		return false;
	}
	FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, OperationId);
	if (!Operation || !Operation->bOpen)
	{
		OutReason = TEXT("That operation isn't open.");
		return false;
	}
	if (Operation->Kind == EPuffersOperationKind::LicensedCannabis && !FPuffersCareerRules::CanCommitRegulatedSale(Save->Owner, OperationId, OutReason))
	{
		return false;
	}
	const EPuffersLedgerResult Result = PostWorld(TxId, FPuffersLedger::ReserveAccount(OperationId), Amount, TEXT("World.Customers"),
		FString::Printf(TEXT("Sales at %s"), *Operation->DisplayName));
	if (Result != EPuffersLedgerResult::Ok)
	{
		OutReason = Result == EPuffersLedgerResult::Duplicate ? TEXT("Already recorded.") : TEXT("Couldn't post the sale.");
		return false;
	}
	Operation = FPuffersCareerRules::FindOperation(Save->Owner, OperationId);
	Operation->NetProfit += Amount;
	RecomputeStanding();
	return true;
}

bool UPuffersCareerSubsystem::ReserveRegulatedSale(FName SaleId, FName OperationId, FString& OutReason)
{
	if (!Save || !FPuffersCareerRules::CanCommitRegulatedSale(Save->Owner, OperationId, OutReason))
	{
		return false;
	}
	if (!Save->Owner.PendingRegulatedSales.Contains(SaleId))
	{
		Save->Owner.PendingRegulatedSales.Add(SaleId);
	}
	return true;
}

bool UPuffersCareerSubsystem::CommitRegulatedSale(FName SaleId, FName OperationId, int32 Amount, FString& OutReason)
{
	if (!Save || !Save->Owner.PendingRegulatedSales.Contains(SaleId))
	{
		OutReason = TEXT("That sale isn't reserved anymore (it may have been released by a license closure).");
		return false;
	}
	// The license gate runs again at commit.
	if (!RecordSale(FName(*FString::Printf(TEXT("SALE_%s"), *SaleId.ToString())), OperationId, Amount, OutReason))
	{
		return false;
	}
	Save->Owner.PendingRegulatedSales.Remove(SaleId);
	return true;
}

EPuffersStrikeResult UPuffersCareerSubsystem::RecordFinding(const FPuffersEnforcementFinding& Finding)
{
	if (!Save)
	{
		return EPuffersStrikeResult::NotQualifying;
	}
	TArray<FName> Released;
	const EPuffersStrikeResult Result = FPuffersCareerRules::RecordFinding(Save->Owner, Finding, Now(), Released);
	switch (Result)
	{
	case EPuffersStrikeResult::FirstStrike:
		Publish(EventLicensesRevoked, Finding.FindingId);
		Notify(FString::Printf(TEXT("Confirmed street bust. Every cannabis license you held is revoked for good. %d reserved sale(s) released. "
			"Ordinary jobs and non-cannabis businesses still work."), Released.Num()));
		Milestone(EPuffersFirstBuildMilestone::VerifiedStrike);
		break;
	case EPuffersStrikeResult::NotQualifying:
		Notify(FString::Printf(TEXT("A %s was recorded. It doesn't affect your licenses."), *PuffersCareer::EnumToString(Finding.Kind)));
		break;
	default:
		break;
	}
	RecomputeStanding();
	SaveNow();
	return Result;
}

bool UPuffersCareerSubsystem::CorrectMistakenIdentity(FName FindingId, const FString& Reason)
{
	if (!Save)
	{
		return false;
	}
	bool bRestored = false;
	if (!FPuffersCareerRules::InvalidateFinding(Save->Owner, FindingId, Reason, bRestored))
	{
		return false;
	}
	Notify(bRestored
		? TEXT("Mistaken identity corrected. Your license eligibility is restored (revoked licenses need a new application).")
		: TEXT("Mistaken identity corrected and logged."));
	Milestone(EPuffersFirstBuildMilestone::MistakenMatchCleared);
	SaveNow();
	return true;
}

TArray<FPuffersOperation> UPuffersCareerSubsystem::GetOperations() const
{
	return Save ? Save->Owner.Operations : TArray<FPuffersOperation>();
}

FPuffersCareerStanding UPuffersCareerSubsystem::GetStanding() const
{
	return Save ? Save->Standing : FPuffersCareerStanding();
}

void UPuffersCareerSubsystem::RecomputeStanding()
{
	if (!Save)
	{
		return;
	}
	FPuffersCareerStanding& S = Save->Standing;
	const FPuffersCareerStanding Before = S;

	S.NetCannabisProfit = 0;
	S.MarketReach = 0;
	S.Territory = 0;
	TSet<FName> LicensedCounties;
	for (const FPuffersOperation& Operation : Save->Owner.Operations)
	{
		if (Operation.Kind != EPuffersOperationKind::NonCannabis)
		{
			S.NetCannabisProfit += Operation.NetProfit;
		}
		if (!Operation.bOpen)
		{
			continue;
		}
		if (Operation.Kind == EPuffersOperationKind::LicensedCannabis && !Operation.bRegulatedSalesClosed)
		{
			S.MarketReach += 10;
			LicensedCounties.Add(Operation.CountyId);
		}
		else if (Operation.Kind == EPuffersOperationKind::Street)
		{
			S.Territory += 10;
		}
	}
	S.MarketReach += 5 * LicensedCounties.Num();

	S.StaffedCapacity = 0;
	S.CrewStanding = 0;
	for (const FPuffersEmployment& E : Save->Workforce.Employment)
	{
		if (E.State != EPuffersEmploymentState::Active)
		{
			continue;
		}
		++S.StaffedCapacity;
		const FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, E.BusinessId);
		if (Operation && Operation->Kind == EPuffersOperationKind::Street)
		{
			S.CrewStanding += FMath::Max(1, E.Trust / 10);
		}
	}

	const FName Phase = FPuffersCareerRules::EvaluatePhase(Config.Phases, Save->Owner, S);
	if (Phase != S.PhaseId)
	{
		S.PhaseId = Phase;
		FPuffersCareerRules::NoteChange(S, FString::Printf(TEXT("Career phase: %s"), *Phase.ToString()));
	}
	if (S.LicensedInfluence() != Before.LicensedInfluence())
	{
		FPuffersCareerRules::NoteChange(S, FString::Printf(TEXT("Licensed influence %d -> %d (market reach %d, civic support %d)"),
			Before.LicensedInfluence(), S.LicensedInfluence(), S.MarketReach, S.CivicSupport));
	}
	if (S.StreetInfluence() != Before.StreetInfluence())
	{
		FPuffersCareerRules::NoteChange(S, FString::Printf(TEXT("Street influence %d -> %d (territory %d, crew %d)"),
			Before.StreetInfluence(), S.StreetInfluence(), S.Territory, S.CrewStanding));
	}
}

TArray<FString> UPuffersCareerSubsystem::GetCareerSummary() const
{
	TArray<FString> Lines;
	if (!Save)
	{
		return Lines;
	}
	const FPuffersCareerStanding& S = Save->Standing;
	Lines.Add(FString::Printf(TEXT("Route: %s"), *FPuffersCareerRules::RouteName(GetRoute())));
	Lines.Add(FString::Printf(TEXT("Phase: %s"), S.PhaseId.IsNone() ? TEXT("First paycheck") : *S.PhaseId.ToString()));
	Lines.Add(FString::Printf(TEXT("Pocket %lld, bank %lld. Lifetime wages %lld."), GetBalance(FPuffersLedger::Pocket), GetBalance(FPuffersLedger::Bank), S.LifetimeWages));
	Lines.Add(FString::Printf(TEXT("Net cannabis profit %lld. Staff %d."), S.NetCannabisProfit, S.StaffedCapacity));
	Lines.Add(FString::Printf(TEXT("Licensed influence %d (reach %d + civic %d). Street influence %d (territory %d + crew %d)."),
		S.LicensedInfluence(), S.MarketReach, S.CivicSupport, S.StreetInfluence(), S.Territory, S.CrewStanding));
	for (const FPuffersOperation& Operation : Save->Owner.Operations)
	{
		Lines.Add(FString::Printf(TEXT("  %s [%s] %s%s, profit %lld"), *Operation.DisplayName, *PuffersCareer::EnumToString(Operation.Kind),
			Operation.bOpen ? TEXT("open") : TEXT("not open"), Operation.bRegulatedSalesClosed ? TEXT(", licensed sales closed") : TEXT(""), Operation.NetProfit));
	}
	for (const FString& Change : S.RecentChanges)
	{
		Lines.Add(FString::Printf(TEXT("  - %s"), *Change));
	}
	return Lines;
}

// ---------------------------------------------------------------------------------------------------------- Hooks

void UPuffersCareerSubsystem::OnGameEvent(FName EventName, FName Context)
{
	HandleEvent(EventName, Context);
}

void UPuffersCareerSubsystem::OnObjectiveUpdated(FName MissionId, FName ObjectiveId, int32 Progress, int32 Required)
{
	if (Progress >= Required)
	{
		HandleEvent(TEXT("ObjectiveDone"), ObjectiveId);
	}
}

void UPuffersCareerSubsystem::HandleEvent(FName EventName, FName Context)
{
	if (!Save || bRunningHook)
	{
		return;
	}

	// License gate for missions that grant regulated ownership (LC05 and friends).
	if (EventName == UPuffersMissionSubsystem::EventMissionStarted && Config.RegulatedOwnershipMissions.Contains(Context))
	{
		FString Why;
		if (!FPuffersCareerRules::CanHoldCannabisLicense(Save->Owner, Why))
		{
			Notify(Why);
			if (FailActiveMission)
			{
				FailActiveMission(TEXT("LicenseForfeited"));
			}
			return;
		}
	}

	TGuardValue<bool> Guard(bRunningHook, true);
	for (int32 Index = 0; Index < HookCatalog.Hooks.Num(); ++Index)
	{
		const FPuffersCareerHook& Hook = HookCatalog.Hooks[Index];
		if (Hook.Event != EventName || (!Hook.Context.IsNone() && Hook.Context != Context))
		{
			continue;
		}
		const FName OnceKey = FName(*FString::Printf(TEXT("HOOK_%d_%s"), Index, *Context.ToString()));
		if (Hook.bOnce && Save->CommittedOps.Contains(OnceKey))
		{
			continue;
		}
		for (const FString& Action : Hook.Actions)
		{
			RunAction(Action, Context);
		}
		if (Hook.bOnce)
		{
			Save->CommittedOps.Add(OnceKey);
		}
	}
	SaveNow();
}

bool UPuffersCareerSubsystem::RunAction(const FString& Action, FName Context)
{
	if (!Save)
	{
		return false;
	}
	TArray<FString> Parts;
	Action.ParseIntoArray(Parts, TEXT(":"), false);
	if (Parts.Num() == 0)
	{
		return false;
	}
	const FString Verb = Parts[0];
	auto Arg = [&Parts](int32 Index, const TCHAR* Default = TEXT("")) -> FString
	{
		return Parts.IsValidIndex(Index) && !Parts[Index].IsEmpty() ? Parts[Index] : FString(Default);
	};
	auto Rest = [&Parts](int32 From) -> FString
	{
		TArray<FString> Tail;
		for (int32 Index = From; Index < Parts.Num(); ++Index)
		{
			Tail.Add(Parts[Index]);
		}
		return FString::Join(Tail, TEXT(":"));
	};
	// Unique per run so a repeatable shift pays every completion, but a single completion pays once.
	const FName RunId = MakeId(*FString::Printf(TEXT("%s_%s"), *Verb, *Context.ToString()));
	FString Why;

	if (Verb == TEXT("PayShift"))
	{
		return PayShift(RunId, FCString::Atoi(*Arg(1, TEXT("200"))), FName(*Arg(2, TEXT("SHIFT"))));
	}
	if (Verb == TEXT("Wallet"))
	{
		return PostWorld(RunId, FPuffersLedger::Pocket, FCString::Atoi64(*Arg(1, TEXT("0"))), TEXT("World.Story"), Rest(2)) == EPuffersLedgerResult::Ok;
	}
	if (Verb == TEXT("CommitStreet"))
	{
		const bool bNew = FPuffersCareerRules::CommitStreet(Save->Owner, Now());
		if (bNew)
		{
			Publish(EventRouteCommitted, TEXT("Street"));
			Milestone(EPuffersFirstBuildMilestone::StreetOwnership);
		}
		return bNew;
	}
	if (Verb == TEXT("StartOperation"))
	{
		const FName ArchetypeId = FName(*Arg(1));
		const FName OperationId = FName(*Arg(2));
		const FName County = FName(*Arg(3));
		const bool bFree = Arg(4) == TEXT("free");
		const FPuffersBusinessArchetype* Archetype = FindArchetype(ArchetypeId);
		if (bFree && Archetype)
		{
			// The mission already charged its own Puff Cash; don't charge again.
			FPuffersBusinessArchetype* Mutable = Config.Archetypes.FindByPredicate([ArchetypeId](const FPuffersBusinessArchetype& A) { return A.Id == ArchetypeId; });
			const int32 Saved = Mutable->EntryCost;
			Mutable->EntryCost = 0;
			const FName Result = StartOperation(OperationId, ArchetypeId, County, NAME_None, FString(), Why);
			Mutable->EntryCost = Saved;
			if (Result.IsNone())
			{
				Notify(Why);
			}
			return !Result.IsNone();
		}
		const FName Result = StartOperation(OperationId, ArchetypeId, County, NAME_None, FString(), Why);
		if (Result.IsNone())
		{
			Notify(Why);
		}
		return !Result.IsNone();
	}
	if (Verb == TEXT("InstallAllFixtures"))
	{
		const FName OperationId = FName(*Arg(1));
		const FPuffersOperation* Operation = FPuffersCareerRules::FindOperation(Save->Owner, OperationId);
		const FPuffersBusinessArchetype* Archetype = Operation ? FindArchetype(Operation->ArchetypeId) : nullptr;
		if (!Archetype)
		{
			return false;
		}
		for (const FName& Fixture : Archetype->RequiredFixtures)
		{
			InstallFixture(OperationId, Fixture);
		}
		return true;
	}
	if (Verb == TEXT("OpenOperation"))
	{
		const bool bOk = OpenOperation(FName(*Arg(1)), Why);
		if (!bOk)
		{
			Notify(Why);
		}
		return bOk;
	}
	if (Verb == TEXT("CloseOperation"))
	{
		return CloseOperation(FName(*Arg(1)));
	}
	if (Verb == TEXT("Sale"))
	{
		const bool bOk = RecordSale(RunId, FName(*Arg(1)), FCString::Atoi(*Arg(2, TEXT("100"))), Why);
		if (!bOk)
		{
			Notify(Why);
		}
		return bOk;
	}
	if (Verb == TEXT("Finding"))
	{
		FPuffersEnforcementFinding Finding;
		Finding.FindingId = RunId;
		PuffersCareer::EnumFromString(Arg(1, TEXT("WitnessClaim")), Finding.Kind);
		Finding.bIdentityConfirmed = Arg(2, TEXT("0")) == TEXT("1");
		Finding.bAuthoritative = Arg(3, TEXT("0")) == TEXT("1");
		Finding.CountyId = FName(*Arg(4, *LawCatalog.DefaultCounty.ToString()));
		RecordFinding(Finding);
		return true;
	}
	if (Verb == TEXT("CorrectLastFinding"))
	{
		for (int32 Index = Save->Owner.Findings.Num() - 1; Index >= 0; --Index)
		{
			if (!Save->Owner.Findings[Index].bInvalidated)
			{
				return CorrectMistakenIdentity(Save->Owner.Findings[Index].FindingId, Rest(1).IsEmpty() ? TEXT("Mistaken identity") : Rest(1));
			}
		}
		return false;
	}
	if (Verb == TEXT("Incident"))
	{
		FPuffersViolentIncident Incident;
		Incident.IncidentId = RunId;
		Incident.CountyId = FName(*Arg(1, *LawCatalog.DefaultCounty.ToString()));
		Incident.At = Now();
		Incident.bVerified = true;
		Incident.Weight = FCString::Atof(*Arg(2, TEXT("1")));
		return ReportViolentIncident(Incident);
	}
	if (Verb == TEXT("Propose"))
	{
		const FName Proposal = Propose(FName(*Arg(1)), FName(*Arg(2, *LawCatalog.DefaultCounty.ToString())), Why);
		if (Proposal.IsNone())
		{
			Notify(Why);
		}
		return !Proposal.IsNone();
	}
	if (Verb == TEXT("Support") || Verb == TEXT("Review") || Verb == TEXT("Judge") || Verb == TEXT("PublishNow"))
	{
		const FName County = FName(*Arg(1, *LawCatalog.DefaultCounty.ToString()));
		bool bAny = false;
		for (int32 Index = 0; Index < Save->Law.Proposals.Num(); ++Index)
		{
			const FPuffersPolicyProposal& P = Save->Law.Proposals[Index];
			if (P.CountyId != County)
			{
				continue;
			}
			const FName ProposalId = P.ProposalId;
			if (Verb == TEXT("Support") && (P.State == EPuffersProposalState::Drafting || P.State == EPuffersProposalState::Review || P.State == EPuffersProposalState::Deferred)
				&& P.Kind == EPuffersProposalKind::PossessionReform)
			{
				bAny |= AddProposalSupport(ProposalId, Context.IsNone() ? RunId : Context, FCString::Atoi(*Arg(2, TEXT("25"))));
			}
			else if (Verb == TEXT("Review") && P.State == EPuffersProposalState::Drafting)
			{
				bAny |= SubmitForReview(ProposalId);
			}
			else if (Verb == TEXT("Judge") && (P.State == EPuffersProposalState::Review || P.State == EPuffersProposalState::Deferred))
			{
				FString Reason;
				RunNpcJudge(ProposalId, Reason);
				bAny = true;
			}
			else if (Verb == TEXT("PublishNow") && P.State == EPuffersProposalState::Scheduled)
			{
				// Test Yard fast-forward: activation now instead of after the published delay.
				Save->Law.Proposals[Index].ActivationAt = Now();
				bAny = true;
			}
		}
		if (Verb == TEXT("PublishNow"))
		{
			PublishDueLaws();
		}
		return bAny;
	}
	if (Verb == TEXT("Case"))
	{
		FPuffersCaseFacts Facts;
		Facts.CaseId = RunId;
		Facts.CountyId = FName(*Arg(1, *LawCatalog.DefaultCounty.ToString()));
		Facts.ConductAt = Now();
		Facts.CannabisMg = FCString::Atoi64(*Arg(2, TEXT("0")));
		Facts.bFirearmInIncident = Arg(3, TEXT("0")) == TEXT("1");
		const FPuffersCaseOutcome Outcome = OpenCase(Facts);
		Notify(Outcome.Explanation);
		const float CourtYears = FCString::Atof(*Arg(4, TEXT("0")));
		if (Outcome.bCharge && (CourtYears > 0.f || Outcome.bFirearmOffense))
		{
			ConvictAndSentence(Facts.CaseId, CourtYears);
		}
		return Outcome.bCharge;
	}
	if (Verb == TEXT("ClockIn"))
	{
		ClockIn();
		return true;
	}
	if (Verb == TEXT("Care"))
	{
		ClockIn();
		return CompleteCareActivity(RunId, FName(*Arg(1, TEXT("EatAndRest"))), Why);
	}
	if (Verb == TEXT("Milestone"))
	{
		EPuffersFirstBuildMilestone Which;
		if (PuffersCareer::EnumFromString(Arg(1), Which))
		{
			Milestone(Which);
			return true;
		}
		return false;
	}
	if (Verb == TEXT("Notice"))
	{
		Notify(Rest(1));
		return true;
	}
	if (Verb == TEXT("GoOnline"))
	{
		ActivateOnlineCareer();
		return Save->Estate.bOnlineCareerActive;
	}
	if (Verb == TEXT("Apartment"))
	{
		return !SubscribeApartment(FName(*Arg(1)), RunId, Why).IsNone();
	}
	if (Verb == TEXT("Deposit"))
	{
		return TransferBetweenAccounts(RunId, FPuffersLedger::Pocket, FPuffersLedger::ReserveAccount(FName(*Arg(1))), FCString::Atoi64(*Arg(2, TEXT("0"))),
			TEXT("Business deposit")) == EPuffersLedgerResult::Ok;
	}
	if (Verb == TEXT("Hire"))
	{
		return !OfferJob(FName(*Arg(1)), FName(*Arg(2)), Arg(5, TEXT("ai")) == TEXT("ai"), TEXT("Staff"), FCString::Atoi(*Arg(3, TEXT("100"))),
			FCString::Atoi(*Arg(4, TEXT("5"))), Why).IsNone();
	}
	if (Verb == TEXT("Payday"))
	{
		return !RunPayroll(FName(*Arg(1)), Now()).IsNone();
	}
	if (Verb == TEXT("SettleWages"))
	{
		return SettleArrears(FName(*Arg(1))) > 0;
	}

	UE_LOG(LogPuffers, Warning, TEXT("Unknown career action '%s'"), *Action);
	return false;
}
