// The career owner: every Rev 12 handoff mechanic behind one authority and one save, so multi-system settlements
// (a raid, a strike, an as-built sale) commit atomically.
//
//   Wallet ............ double-entry Puff Cash with holds and receipts (Pocket mirrors the existing wallet)
//   Route/licensing ... uncommitted -> licensed / street; one strike revokes every cannabis license
//   Operations ........ licensed shops, street operations, non-cannabis businesses; magnate standing and phases
//   County law ........ versioned law, proposals, NPC/server judge, bribery, county sentences -> custody
//   Insiders .......... lawyers, officials, officers: quotes, gifts, consent, knowledge, fallout
//   Care & estate ..... 7-day care window, life insurance, inactivity raids, full-estate transfer
//   Housing ........... apartment and plot subscriptions (mock billing), applications, player leases, as-built sales
//   Workforce ......... secured loans and liens, hiring floor, shifts, weekly payroll, unpaid choices
//
// Modes (Jayde, Nov 25-26 2026): offline play is story missions only. The shared-world economy (operations, housing,
// care, raids, payroll, loans) needs an online world. Route, licensing, county law and insiders work in both, with
// the same rules. In development builds, puffers.Career.LocalAuthority 1 (the default outside Shipping) lets a
// standalone Test Yard act as its own server for playtesting.
//
// Story missions connect through Content/Data/Career/Hooks.json; no mission data has to change.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Career/PuffersLedger.h"
#include "Career/PuffersCareerCatalog.h"
#include "PuffersCareerSubsystem.generated.h"

UCLASS()
class PUFFERSCLUB_API UPuffersCareerSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY(SaveGame)
	int32 SaveVersion = CurrentVersion;

	UPROPERTY(SaveGame)
	FPuffersCareerOwnerState Owner;

	UPROPERTY(SaveGame)
	FPuffersCareerStanding Standing;

	UPROPERTY(SaveGame)
	FPuffersLedger Ledger;

	UPROPERTY(SaveGame)
	FPuffersCountyLawState Law;

	UPROPERTY(SaveGame)
	TMap<FName, FPuffersInsiderRelationship> Insiders;

	UPROPERTY(SaveGame)
	FPuffersEstateState Estate;

	UPROPERTY(SaveGame)
	FPuffersHousingState Housing;

	UPROPERTY(SaveGame)
	FPuffersWorkforceState Workforce;

	// Hooks that already ran (bOnce) and other one-time operations.
	UPROPERTY(SaveGame)
	TSet<FName> CommittedOps;

	UPROPERTY(SaveGame)
	TArray<EPuffersFirstBuildMilestone> Milestones;

	// Phone notifications, newest last.
	UPROPERTY(SaveGame)
	TArray<FString> Notices;

	// Id counter for generated ids.
	UPROPERTY(SaveGame)
	int32 NextSerial = 1;

	// Weekly rollover: each operation's net profit at the last rollover (profitable-week streaks).
	UPROPERTY(SaveGame)
	TMap<FName, int64> ProfitAtLastWeek;

	UPROPERTY(SaveGame)
	FDateTime LastWeekRollover;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuffersCareerNotice, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPuffersCareerEvent, FName, EventName, FName, Context);

UCLASS()
class PUFFERSCLUB_API UPuffersCareerSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static const FString SaveSlotName;

	// Events published on the mission event bus (and OnCareerEvent).
	static const FName EventRouteCommitted;      // Context: route name
	static const FName EventLicenseGranted;      // Context: license id
	static const FName EventLicensesRevoked;     // Context: finding id
	static const FName EventOperationOpened;     // Context: operation id
	static const FName EventLawPublished;        // Context: proposal id
	static const FName EventCountySentence;      // Context: sentence id
	static const FName EventRaidSettled;         // Context: raid id
	static const FName EventPayrollUnpaid;       // Context: business id (AI workers react through their controllers)
	static const FName EventRobTheBossOffered;   // Context: employment id
	static const FName EventInsiderFallout;      // Context: npc id

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject: publishes due laws, advances tenancies/plots/loans, runs pay day, tracks active time.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career")
	int32 ReloadData();

	// ---------------------------------------------------------------- Modes

	// Shared-world economy available (online world, or the local-authority dev switch).
	UFUNCTION(BlueprintPure, Category = "Puffers|Career")
	bool IsSharedWorld() const;

	// ---------------------------------------------------------------- Wallet

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Wallet")
	int64 GetBalance(FName Account) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Wallet")
	int64 GetAvailable(FName Account) const;

	// Pocket -> Bank, Bank -> Reserve.<Business>, and so on. Total owned funds don't change.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult TransferBetweenAccounts(FName TxId, FName From, FName To, int64 Amount, const FString& Memo);

	// Money arriving from the NPC economy (wages, customers) or leaving to it (purchases, fees).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult PostWorld(FName TxId, FName Account, int64 Amount, FName WorldAccount, const FString& Memo);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult PlaceHold(FName HoldId, FName Account, int64 Amount, const FString& Memo);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult CaptureHold(FName HoldId, FName TxId, FName To, int64 Amount);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult ReleaseHold(FName HoldId);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Wallet")
	EPuffersLedgerResult ReverseTransaction(FName TxId, FName NewTxId, const FString& Memo);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Wallet")
	FPuffersReceipt GetReceipt(FName TxId) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Wallet")
	TArray<FPuffersLedgerTransaction> GetRecentTransactions(int32 Count) const;

	// Brings Pocket in line with the existing Puff Cash wallet (mission rewards, job pay, shop takings).
	void SyncPocket();

	// ---------------------------------------------------------------- Free roam: earn

	// A paid ordinary shift. Pays once per op id. Counts as care if the player clocked in. Never commits a route.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Work")
	bool PayShift(FName OpId, int32 Wage, FName JobId);

	// ---------------------------------------------------------------- Route and licensing

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Route")
	EPuffersCareerRoute GetRoute() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Route")
	FString GetCommitmentText(EPuffersOperationKind Kind) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Route")
	bool CanHoldCannabisLicense(FString& OutReason) const;

	// Opens an operation of an archetype (charges the entry cost once). Street archetypes take the permanent commitment;
	// licensed ones apply for the county license in the same step. Returns the operation id or None.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	FName StartOperation(FName OpId, FName ArchetypeId, FName CountyId, FName PremisesId, const FString& DisplayName, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool InstallFixture(FName OperationId, FName FixtureId);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool OpenOperation(FName OperationId, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool CloseOperation(FName OperationId);

	// Records customer income for an operation (World.Customers -> Reserve). Licensed sales go through the license gate.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool RecordSale(FName TxId, FName OperationId, int32 Amount, FString& OutReason);

	// Prepaid licensed sale: hold now, commit later. A revocation in between releases it once.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool ReserveRegulatedSale(FName SaleId, FName OperationId, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool CommitRegulatedSale(FName SaleId, FName OperationId, int32 Amount, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	EPuffersStrikeResult RecordFinding(const FPuffersEnforcementFinding& Finding);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Route")
	bool CorrectMistakenIdentity(FName FindingId, const FString& Reason);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Route")
	TArray<FPuffersOperation> GetOperations() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Route")
	FPuffersCareerStanding GetStanding() const;

	// Lines for the career screen: route, phase, influence, and why standing changed.
	UFUNCTION(BlueprintPure, Category = "Puffers|Career")
	TArray<FString> GetCareerSummary() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career")
	bool HasMilestone(EPuffersFirstBuildMilestone Milestone) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career")
	TArray<FString> GetFirstBuildChecklist() const;

	// ---------------------------------------------------------------- County law

	// County id for a world location (via the map's county slices), or the catalog default.
	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Law")
	FName GetCountyAt(const FVector& Location) const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Law")
	FString DescribeCurrentLaw(FName CountyId) const;

	// Drafts a proposal from a template. A reform needs a licensed owner with civic access in that county.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	FName Propose(FName TemplateId, FName CountyId, FString& OutReason);

	// Meetings, testimony, worker and neighborhood relationships, completed commitments. Each source counts once.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	bool AddProposalSupport(FName ProposalId, FName SourceId, int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	bool SubmitForReview(FName ProposalId);

	// Verified violent street deals. A spike past the county threshold opens a firearm-minimum review automatically.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	bool ReportViolentIncident(const FPuffersViolentIncident& Incident);

	// NPC judge (offline, or as the online fallback).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	EPuffersJudgeDecision RunNpcJudge(FName ProposalId, FString& OutReason);

	// An authorized server judge. Refused outside an online authority.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	bool ServerJudgeDecide(FName ProposalId, EPuffersJudgeDecision Decision, FName JudgeId, const FString& Reason);

	// Publishes scheduled versions whose time has come. Called from Tick too.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	int32 PublishDueLaws();

	// Records a case under the law in force at the conduct.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	FPuffersCaseOutcome OpenCase(const FPuffersCaseFacts& Facts);

	// Convicts and sentences: the court's years, floored by the firearm minimum, become custody blocks at the current
	// custody scale and go to the sentence service. Fines are charged once.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	FName ConvictAndSentence(FName CaseId, float CourtYears);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Law")
	EPuffersBribeOutcome OfferBribe(FName OfferId, FName OfficialId, int32 Amount, FName Scope);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Law")
	bool HasDiscretionaryFavor(FName OfficialId, FName Scope) const;

	// ---------------------------------------------------------------- Insiders

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	FPuffersInsiderRequest QuoteInsider(FName NpcId, FName RequestId, FString& OutWhy);

	// Accepts a quote (or a counter). Holds the fee; nothing is charged until completion.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	bool AcceptInsiderQuote(FName NpcId, FName QuoteId, FString& OutWhy);

	// Settles once. Rechecks role, county and access. Success charges the fee; failure/abandon releases it.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	EPuffersRequestState CompleteInsiderRequest(FName NpcId, FName QuoteId, bool bSucceeded);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	bool GiveInsiderGift(FName NpcId, FName ItemId, bool bProfessionalScene, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	bool AskInsiderForIntimacy(FName NpcId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	void StopInsiderIntimacy(FName NpcId);

	// The player tells the insider something (a fact or a claim).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	bool ShareWithInsider(FName NpcId, FName Subject, bool bIsClaim, bool bMaterial);

	// The insider directly observes a tagged scene detail.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	bool InsiderObserves(FName NpcId, FName Subject, FName SceneId, bool bMaterial);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	void InsiderPromise(FName NpcId, FName PromiseId, bool bKept);

	// A grievance (public humiliation, exposing the relationship...). May trigger fallout.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	EPuffersFallout WrongInsider(FName NpcId, FName Grievance, int32 Resentment);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Insiders")
	void BreakUpWithInsider(FName NpcId);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Insiders")
	bool GetInsider(FName NpcId, FPuffersInsiderRelationship& Out) const;

	// ---------------------------------------------------------------- Care, insurance, raids

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Care")
	bool ActivateOnlineCareer();

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Care")
	EPuffersCareStatus GetCareStatus() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Care")
	FDateTime GetCareDeadline() const;

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Care")
	void ClockIn();

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Care")
	bool CompleteCareActivity(FName OpId, FName ActivityId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Care")
	FName BuyInsurance(FName ProductId, FName InsuredId, FName BeneficiaryId, bool bInsuredConsents, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Care")
	bool FileInsuranceClaim(FName PolicyId, const FPuffersLossEvent& Loss, FString& OutWhy);

	// Another player starts a raid on this estate. One active raid at a time.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Raids")
	bool StartRaid(FName RaidId, FName RaiderId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Raids")
	bool EndRaid(FName RaidId, EPuffersRaidState Outcome);

	// Server-verified success. Rechecks care, coverage and residence, then transfers the whole eligible estate once.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Raids")
	bool SettleRaidSuccess(FName RaidId, FString& OutWhy);

	// What a raid would take right now (and what stays).
	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Raids")
	TArray<FPuffersEstateLine> BuildEstateManifest() const;

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Raids")
	bool HasQualifyingResidence() const;

	// ---------------------------------------------------------------- Housing

	// Starts a City Sample apartment subscription (mock billing confirms the first month).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	FName SubscribeApartment(FName UnitId, FName BillingEventId, FString& OutWhy);

	// A billing callback for an apartment or plot entitlement. Repeats are ignored.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool ApplyBillingEvent(FName EntitlementId, FName EventId, bool bPaid, bool bPending);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool StoreInApartment(FName TenancyId, FName ItemId, bool bPlace, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool TakeFromApartment(FName TenancyId, FName ItemInstanceId, FString& OutWhy);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Housing")
	bool CanEnterApartment(FName TenancyId) const;

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	FName ApplyForVacancy(FName UnitId, FName ArchetypeId, const FString& BusinessName, FString& OutWhy);

	// Charges the upfront amount once and opens the fit-out (creates the operation).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	FName TakePossession(FName ApplicationId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool BuyPlot(FName PlotId, FName BillingEventId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool AddPlotBuilding(FName PlotId, FName BuildingId, FName Use, bool bCompleted, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	FName LeaseToPlayer(FName PlotId, FName BuildingId, FName TenantId, int32 Rent, int32 Deposit, int32 PeriodDays, FString& OutWhy);

	// Posts tenant rent to the landlord once per period (for every lease due).
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	int32 CollectLeaseRent();

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool CurePlot(FName PlotId, FName BillingEventId);

	// Another player buys a lapsed plot as built. Liens settle first, then the owner gets the rest.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Housing")
	bool BuyPlotAsBuilt(FName PlotId, FName BuyerId, const TArray<int32>& ExpectedVersions, FString& OutWhy);

	// ---------------------------------------------------------------- Workforce

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Loans")
	FName QuoteLoan(FName LenderId, const FPuffersCollateral& Collateral, int64 Principal, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Loans")
	bool AcceptLoan(FName LoanId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Loans")
	int64 RepayLoan(FName LoanId, FName TxId, int64 Amount);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Loans")
	bool IsAssetLiened(FName AssetId) const;

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	FName OfferJob(FName BusinessId, FName WorkerId, bool bIsAI, const FString& Title, int32 WagePerShift, int32 ShiftsPerWeek, FString& OutWhy);

	// Rechecks the hiring floor in the same step that creates the employment.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool AcceptJobOffer(FName EmploymentId, FString& OutWhy);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	FName PublishShift(FName BusinessId, FName EmploymentId, FDateTime StartsAt, FDateTime EndsAt, const FString& Role);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool RespondToShift(FName ShiftId, bool bAccept);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool CheckInShift(FName ShiftId);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool CompleteShift(FName ShiftId, int32 TasksDone);

	// The worker didn't show: an eligible AI substitute takes the shift as their own NPC.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool SubstituteShift(FName ShiftId, FName SubstituteEmploymentId);

	// Runs the weekly pay run for a business once per cutoff.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	FName RunPayroll(FName BusinessId, FDateTime Cutoff);

	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	bool ChooseAfterUnpaid(FName EmploymentId, EPuffersUnpaidChoice Choice);

	// A top-up that settles outstanding wages from the business reserve.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career|Payroll")
	int64 SettleArrears(FName BusinessId);

	UFUNCTION(BlueprintPure, Category = "Puffers|Career|Payroll")
	TArray<FPuffersEmployment> GetEmployment(FName BusinessId) const;

	// ---------------------------------------------------------------- Misc

	UFUNCTION(BlueprintPure, Category = "Puffers|Career")
	TArray<FString> GetNotices(int32 Count) const;

	// Runs a hook action string ("PayShift:220:SHOP_STAFF"). Exposed for debug tools and tests.
	UFUNCTION(BlueprintCallable, Category = "Puffers|Career")
	bool RunAction(const FString& Action, FName Context);

	UPROPERTY(BlueprintAssignable, Category = "Puffers|Career")
	FPuffersCareerNotice OnNotice;

	UPROPERTY(BlueprintAssignable, Category = "Puffers|Career")
	FPuffersCareerEvent OnCareerEvent;

	bool SaveNow();
	UPuffersCareerSaveGame* GetSave() const { return Save; }

	// ---------------------------------------------------------------- Test and integration hooks

	void InitializeForTests(const FPuffersCareerConfig& InConfig, const FPuffersCountyLawCatalog& InLaw,
		const FPuffersInsiderCatalog& InInsiders, const FPuffersHousingCatalog& InHousing, const FPuffersCareerHookCatalog& InHooks);

	// Server clock (UTC). Tests drive it.
	TFunction<FDateTime()> Now = []() { return FDateTime::UtcNow(); };
	// The existing Puff Cash wallet (UPuffersMissionSubsystem).
	TFunction<int64()> GetWalletCash;
	TFunction<void(int64)> AddWalletCash;
	// Shared-world authority override (tests set it; null = net mode / CVar).
	TFunction<bool()> SharedWorldOverride;
	// The career owner's stable id (player account id online, "LocalCareer" offline).
	FName CareerOwnerId = TEXT("LocalCareer");
	// Hands a generated county sentence to the custody service. Returns true if imposed.
	TFunction<bool(const FPuffersSentenceDefinition&)> ImposeCustody;
	// Inventory hooks for the estate and apartment storage (item id -> count).
	TFunction<TMap<FName, int32>()> GetInventory;
	TFunction<bool(FName, int32)> RemoveInventory;
	TFunction<void(FName, int32)> AddInventory;
	// Vehicle titles for the estate (garage integration; unbound until the garage exposes transfers).
	TFunction<TArray<FName>()> GetOwnedVehicles;
	TFunction<bool(FName, FName)> TransferVehicle;
	// Map county lookup (UPuffersLawSubsystem::GetCountyAt).
	TFunction<FName(const FVector&)> MapCountyAt;
	// Event bus (UPuffersMissionSubsystem::BroadcastGameEvent).
	TFunction<void(FName, FName)> Broadcast;
	// Fails the active mission (license gate on regulated-ownership missions).
	TFunction<void(FName)> FailActiveMission;

	// Due laws, tenancies, plots, loans, lease rent and the weekly rollover. Tick runs it every 5 s; tests call it after moving the clock.
	void RunHousekeeping();

	// Weekly rollover for profitable-week streaks and pay day. Called from RunHousekeeping; tests call it directly.
	void WeeklyRollover();

	// Called by the mission subsystem bindings (and tests) to run hooks.
	void HandleEvent(FName EventName, FName Context);

	// World map: the catalog listing for a plot (county, cells, build-zone lot).
	const FPuffersPlotListing* GetPlotListing(FName PlotId) const { return FindPlotListing(PlotId); }

private:
	UFUNCTION()
	void OnGameEvent(FName EventName, FName Context);

	UFUNCTION()
	void OnObjectiveUpdated(FName MissionId, FName ObjectiveId, int32 Progress, int32 Required);

	void LoadOrCreate();
	FName MakeId(const TCHAR* Prefix);
	void Notify(const FString& Text);
	void Publish(FName EventName, FName Context);
	void Milestone(EPuffersFirstBuildMilestone Which);
	bool RequireShared(FString& OutWhy) const;
	bool HasAuthority() const;

	EPuffersLedgerResult PostLedger(TFunctionRef<EPuffersLedgerResult(FPuffersLedger&)> Op);
	void MirrorPocketDelta(int64 PocketBefore);

	const FPuffersBusinessArchetype* FindArchetype(FName Id) const;
	const FPuffersCountyDefinition* FindCounty(FName CountyId) const;
	const FPuffersProposalTemplate* FindTemplate(FName Id) const;
	const FPuffersInsiderDefinition* FindInsider(FName NpcId) const;
	const FPuffersInsiderRequestDefinition* FindRequest(FName RequestId) const;
	const FPuffersInsuranceProduct* FindProduct(FName Id) const;
	const FPuffersLenderDefinition* FindLender(FName Id) const;
	const FPuffersApartmentListing* FindApartment(FName UnitId) const;
	const FPuffersCommercialVacancy* FindVacancy(FName UnitId) const;
	const FPuffersPlotListing* FindPlotListing(FName PlotId) const;
	FPuffersEntitlement* FindEntitlement(FName EntitlementId);
	const FPuffersEntitlement* FindEntitlement(FName EntitlementId) const;
	FPuffersInsiderRelationship& Relationship(FName NpcId);
	bool CountyAllowsCorruption(FName CountyId) const;
	bool HasCivicAccess(FName CountyId) const;
	void RecomputeStanding();
	void OpenFirearmReviewIfDue(FName CountyId);
	void AdvanceHousing();
	void AdvanceLoans();
	void ReRegisterCountySentences();

	FPuffersCareerConfig Config;
	FPuffersCountyLawCatalog LawCatalog;
	FPuffersInsiderCatalog InsiderCatalog;
	FPuffersHousingCatalog HousingCatalog;
	FPuffersCareerHookCatalog HookCatalog;

	UPROPERTY()
	TObjectPtr<UPuffersCareerSaveGame> Save;

	float SecondsSinceHousekeeping = 0.f;
	float SecondsSinceSave = 0.f;
	bool bRunningHook = false;
	bool bForTests = false;
};
