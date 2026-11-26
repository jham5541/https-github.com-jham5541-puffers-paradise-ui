// Career systems from the Rev 12 handoff: ledger, route and one-strike licensing, county law, bribes, care and raids,
// apartments, plots, loans and payroll. The fixture loads the real Content/Data/Career files, so these also check the data.
// Run: Tools > Session Frontend > Automation, filter "Puffers.Career".

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "Career/PuffersCareerSubsystem.h"
#include "Career/PuffersCareerRules.h"
#include "Career/PuffersCountyLawRules.h"
#include "Career/PuffersWorkforceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PUFFERS_CAREER_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PuffersCareerTests
{
	// Equality check that works for enums and names too (TestEqual needs a LexToString for its message).
	template <typename A, typename B>
	bool Same(FAutomationTestBase& Test, const TCHAR* What, const A& Actual, const B& Expected)
	{
		return Test.TestTrue(What, Actual == Expected);
	}

	template <typename T>
	bool LoadData(const TCHAR* File, T& Out)
	{
		FString Json;
		return FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/Career") / File))
			&& FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Out, 0, 0);
	}

	struct FFixture
	{
		UPuffersCareerSubsystem* Career = nullptr;
		FDateTime Clock = FDateTime(2027, 1, 4, 12, 0, 0);
		int64 Wallet = 0;
		TArray<TPair<FName, FName>> Events;
		TArray<FName> VehiclesTaken;
		bool bLoaded = true;

		FPuffersCareerConfig Config;
		FPuffersCountyLawCatalog Law;
		FPuffersInsiderCatalog Insiders;
		FPuffersHousingCatalog Housing;
		FPuffersCareerHookCatalog Hooks;

		explicit FFixture(TFunctionRef<void(FFixture&)> Tweak = [](FFixture&) {})
		{
			bLoaded &= LoadData(TEXT("Career.json"), Config);
			bLoaded &= LoadData(TEXT("CountyLaws.json"), Law);
			bLoaded &= LoadData(TEXT("Insiders.json"), Insiders);
			bLoaded &= LoadData(TEXT("Housing.json"), Housing);
			bLoaded &= LoadData(TEXT("Hooks.json"), Hooks);
			Tweak(*this);

			Career = NewObject<UPuffersCareerSubsystem>(GetTransientPackage());
			Career->AddToRoot();
			Career->Now = [this]() { return Clock; };
			Career->GetWalletCash = [this]() { return Wallet; };
			Career->AddWalletCash = [this](int64 Delta) { Wallet += Delta; };
			Career->Broadcast = [this](FName Event, FName Context) { Events.Emplace(Event, Context); };
			Career->TransferVehicle = [this](FName Vehicle, FName) { VehiclesTaken.Add(Vehicle); return true; };
			Career->InitializeForTests(Config, Law, Insiders, Housing, Hooks);
		}

		~FFixture()
		{
			Career->RemoveFromRoot();
		}

		UPuffersCareerSaveGame* Save() const { return Career->GetSave(); }

		void Advance(double Hours)
		{
			Clock += FTimespan::FromHours(Hours);
			Career->RunHousekeeping();
		}

		bool Published(FName Event) const
		{
			return Events.ContainsByPredicate([Event](const TPair<FName, FName>& E) { return E.Key == Event; });
		}

		bool AnyNotice(const TCHAR* Fragment) const
		{
			return Save()->Notices.ContainsByPredicate([Fragment](const FString& N) { return N.Contains(Fragment); })
				|| Save()->Workforce.Notices.ContainsByPredicate([Fragment](const FString& N) { return N.Contains(Fragment); });
		}

		FPuffersEnforcementFinding Finding(const TCHAR* Id, EPuffersFindingKind Kind, bool bConfirmed, bool bAuthoritative) const
		{
			FPuffersEnforcementFinding F;
			F.FindingId = Id;
			F.Kind = Kind;
			F.bIdentityConfirmed = bConfirmed;
			F.bAuthoritative = bAuthoritative;
			F.CountyId = TEXT("Central");
			F.At = Clock;
			return F;
		}

		FPuffersCaseOutcome Case(const TCHAR* Id, int64 Mg, bool bFirearm = false)
		{
			FPuffersCaseFacts Facts;
			Facts.CaseId = Id;
			Facts.CountyId = TEXT("Central");
			Facts.ConductAt = Clock;
			Facts.CannabisMg = Mg;
			Facts.bFirearmInIncident = bFirearm;
			return Career->OpenCase(Facts);
		}
	};
}

using namespace PuffersCareerTests;

// ------------------------------------------------------------------------------------------------------------- Data

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerData, "Puffers.Career.Data.LoadsAndMatchesDecisions", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerData::RunTest(const FString&)
{
	FFixture F;
	TestTrue(TEXT("All five career data files load"), F.bLoaded);
	Same(*this, TEXT("Care window is 7 days (Jayde, Nov 26)"), F.Config.CareWindowHours, 168.f);
	Same(*this, TEXT("Five counties"), F.Law.Counties.Num(), 5);
	for (const TCHAR* Name : { TEXT("Central"), TEXT("Heights"), TEXT("Southside"), TEXT("Frontier"), TEXT("Coast") })
	{
		TestTrue(FString::Printf(TEXT("County %s exists"), Name), F.Law.Counties.ContainsByPredicate([Name](const FPuffersCountyDefinition& C) { return C.CountyId == Name; }));
	}
	for (const FPuffersCountyDefinition& County : F.Law.Counties)
	{
		Same(*this, TEXT("Seed fine-only cap is 3500 mg"), County.SeedLaw.FineOnlyCapMg, FPuffersCountyLawRules::SeedFineOnlyCapMg);
		TestTrue(TEXT("Each county maps to a map slice"), County.MapCountyIds.Num() > 0);
	}
	TestTrue(TEXT("LC05 goes through the license gate"), F.Config.RegulatedOwnershipMissions.Contains(FName(TEXT("LC05"))));

	// Every hook verb is one RunAction knows.
	const TSet<FString> Verbs = { TEXT("PayShift"), TEXT("Wallet"), TEXT("CommitStreet"), TEXT("StartOperation"), TEXT("InstallAllFixtures"), TEXT("OpenOperation"),
		TEXT("CloseOperation"), TEXT("Sale"), TEXT("Finding"), TEXT("CorrectLastFinding"), TEXT("Incident"), TEXT("Propose"), TEXT("Support"), TEXT("Review"),
		TEXT("Judge"), TEXT("PublishNow"), TEXT("Case"), TEXT("ClockIn"), TEXT("Care"), TEXT("Milestone"), TEXT("Notice"), TEXT("GoOnline"), TEXT("Apartment"),
		TEXT("Deposit"), TEXT("Hire"), TEXT("Payday"), TEXT("SettleWages") };
	for (const FPuffersCareerHook& Hook : F.Hooks.Hooks)
	{
		for (const FString& Action : Hook.Actions)
		{
			FString Verb, Rest;
			if (!Action.Split(TEXT(":"), &Verb, &Rest))
			{
				Verb = Action;
			}
			TestTrue(FString::Printf(TEXT("Hook verb %s is known"), *Verb), Verbs.Contains(Verb));
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Ledger

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerLedger, "Puffers.Career.Ledger.DoubleEntryHoldsAndIdempotency", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerLedger::RunTest(const FString&)
{
	FPuffersLedger L;
	const FDateTime T = FDateTime(2027, 1, 1);
	Same(*this, TEXT("Income posts"), L.Transfer(TEXT("TX1"), TEXT("World.Customers"), FPuffersLedger::Pocket, 1000, TEXT("Sale"), T), EPuffersLedgerResult::Ok);
	Same(*this, TEXT("A repeated tx id is a no-op"), L.Transfer(TEXT("TX1"), TEXT("World.Customers"), FPuffersLedger::Pocket, 1000, TEXT("Sale"), T), EPuffersLedgerResult::Duplicate);
	Same(*this, TEXT("Balance counted once"), L.GetBalance(FPuffersLedger::Pocket), 1000ll);

	Same(*this, TEXT("Hold placed"), L.PlaceHold(TEXT("H1"), FPuffersLedger::Pocket, 700, TEXT("Escrow"), T), EPuffersLedgerResult::Ok);
	TestTrue(TEXT("Can't spend held funds"), L.Transfer(TEXT("TX2"), FPuffersLedger::Pocket, TEXT("World.Market"), 400, TEXT("Buy"), T) != EPuffersLedgerResult::Ok);
	Same(*this, TEXT("Available excludes the hold"), L.GetAvailable(FPuffersLedger::Pocket), 300ll);
	Same(*this, TEXT("Capture"), L.CaptureHold(TEXT("H1"), TEXT("TX3"), TEXT("World.Lawyer"), 700, T), EPuffersLedgerResult::Ok);
	Same(*this, TEXT("Captured once"), L.GetBalance(FPuffersLedger::Pocket), 300ll);

	Same(*this, TEXT("Owned-to-owned keeps total"), L.Transfer(TEXT("TX4"), FPuffersLedger::Pocket, FPuffersLedger::Bank, 200, TEXT("Deposit"), T), EPuffersLedgerResult::Ok);
	Same(*this, TEXT("Total owned unchanged"), L.GetTotalOwned(), 300ll);
	Same(*this, TEXT("Reverse"), L.Reverse(TEXT("TX4"), TEXT("TX4R"), TEXT("Undo"), T), EPuffersLedgerResult::Ok);
	Same(*this, TEXT("Bank back to zero"), L.GetBalance(FPuffersLedger::Bank), 0ll);
	TestTrue(TEXT("Owned accounts can't go negative"), L.Transfer(TEXT("TX5"), FPuffersLedger::Pocket, TEXT("World.Market"), 301, TEXT("Overdraw"), T) != EPuffersLedgerResult::Ok);
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Route

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerOneStrike, "Puffers.Career.Route.OneStrikeRevokesEveryLicense", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerOneStrike::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 5000;
	FString Why;
	Same(*this, TEXT("Starts uncommitted"), F.Career->GetRoute(), EPuffersCareerRoute::Uncommitted);
	TestTrue(TEXT("A paid shift never commits a route"), F.Career->PayShift(TEXT("S1"), 200, TEXT("SHOP_STAFF")));
	Same(*this, TEXT("Still uncommitted"), F.Career->GetRoute(), EPuffersCareerRoute::Uncommitted);

	Same(*this, TEXT("Licensed shop starts"), F.Career->StartOperation(TEXT("OP_SHOP"), TEXT("CANNABIS_SHOP"), TEXT("Central"), NAME_None, FString(), Why), FName(TEXT("OP_SHOP")));
	Same(*this, TEXT("Licensed only"), F.Career->GetRoute(), EPuffersCareerRoute::LicensedOnly);
	Same(*this, TEXT("Street op starts"), F.Career->StartOperation(TEXT("OP_CORNER"), TEXT("STREET_OP"), TEXT("Central"), NAME_None, FString(), Why), FName(TEXT("OP_CORNER")));
	Same(*this, TEXT("Dual eligible"), F.Career->GetRoute(), EPuffersCareerRoute::StreetDualEligible);
	Same(*this, TEXT("Entry costs charged once each"), F.Wallet, 5000ll + 200 - 1500 - 800);
	TestTrue(TEXT("Dual operation milestone"), F.Career->HasMilestone(EPuffersFirstBuildMilestone::DualOperation));

	Same(*this, TEXT("An unconfirmed claim doesn't count"),
		F.Career->RecordFinding(F.Finding(TEXT("F0"), EPuffersFindingKind::WitnessClaim, false, false)), EPuffersStrikeResult::NotQualifying);
	Same(*this, TEXT("A confirmed bust is the first strike"),
		F.Career->RecordFinding(F.Finding(TEXT("F1"), EPuffersFindingKind::QualifyingStreetActivity, true, true)), EPuffersStrikeResult::FirstStrike);
	Same(*this, TEXT("Forfeited"), F.Career->GetRoute(), EPuffersCareerRoute::StreetForfeited);
	TestFalse(TEXT("No new licenses"), F.Career->CanHoldCannabisLicense(Why));
	TestTrue(TEXT("Revocation published"), F.Published(UPuffersCareerSubsystem::EventLicensesRevoked));
	for (const FPuffersLicense& License : F.Save()->Owner.Licenses)
	{
		Same(*this, TEXT("Every license revoked"), License.State, EPuffersLicenseState::Revoked);
	}
	Same(*this, TEXT("Same finding again is a duplicate"),
		F.Career->RecordFinding(F.Finding(TEXT("F1"), EPuffersFindingKind::QualifyingStreetActivity, true, true)), EPuffersStrikeResult::Duplicate);
	Same(*this, TEXT("Second bust stays on the record"),
		F.Career->RecordFinding(F.Finding(TEXT("F2"), EPuffersFindingKind::QualifyingStreetActivity, true, true)), EPuffersStrikeResult::AdditionalStrike);
	TestFalse(TEXT("Non-cannabis businesses still work"),
		F.Career->StartOperation(TEXT("OP_BODEGA"), TEXT("BODEGA"), TEXT("Central"), NAME_None, FString(), Why).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerMistakenIdentity, "Puffers.Career.Route.MistakenIdentityRestoresEligibility", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerMistakenIdentity::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 2000;
	FString Why;
	F.Career->StartOperation(TEXT("OP_SHOP"), TEXT("CANNABIS_SHOP"), TEXT("Central"), NAME_None, FString(), Why);
	Same(*this, TEXT("An outfit match alone isn't a strike"),
		F.Career->RecordFinding(F.Finding(TEXT("F0"), EPuffersFindingKind::OutfitMatch, false, false)), EPuffersStrikeResult::NotQualifying);
	TestTrue(TEXT("Still licensable"), F.Career->CanHoldCannabisLicense(Why));

	F.Career->RecordFinding(F.Finding(TEXT("F1"), EPuffersFindingKind::QualifyingStreetActivity, true, true));
	TestFalse(TEXT("Strike bars licenses"), F.Career->CanHoldCannabisLicense(Why));
	TestTrue(TEXT("Correction accepted"), F.Career->CorrectMistakenIdentity(TEXT("F1"), TEXT("Footage shows someone else")));
	TestTrue(TEXT("Eligibility restored when no other strike remains"), F.Career->CanHoldCannabisLicense(Why));
	TestTrue(TEXT("Milestone"), F.Career->HasMilestone(EPuffersFirstBuildMilestone::MistakenMatchCleared));
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Law

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerLawCaps, "Puffers.Career.Law.CapsAreInclusive", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerLawCaps::RunTest(const FString&)
{
	FPuffersCountyLawVersion Law;
	Law.CountyId = TEXT("Central");
	Law.FineOnlyCapMg = FPuffersCountyLawRules::SeedFineOnlyCapMg;
	FPuffersPossessionBand Band;
	Band.MaxMg = -1;
	Band.Fine = 1000;
	Band.CustodyYears = 2.f;
	Law.Bands.Add(Band);

	FPuffersCaseFacts Facts;
	Facts.CannabisMg = 3500;
	Same(*this, TEXT("3500 mg is fine-only"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).PossessionCustodyYears, 0.f);
	Facts.CannabisMg = 3501;
	TestTrue(TEXT("3501 mg can mean custody"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).PossessionCustodyYears > 0.f);

	Law.FineOnlyCapMg = FPuffersCountyLawRules::SixPoundsMg;
	Facts.CannabisMg = FPuffersCountyLawRules::SixPoundsMg;
	Same(*this, TEXT("6 lb is fine-only after reform"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).PossessionCustodyYears, 0.f);
	Facts.CannabisMg = FPuffersCountyLawRules::SixPoundsMg + 1;
	TestTrue(TEXT("6 lb + 1 mg isn't"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).PossessionCustodyYears > 0.f);

	Facts.bDocumentedLicensedInventory = true;
	TestFalse(TEXT("Documented licensed inventory isn't possession"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).bCharge);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerFirearm, "Puffers.Career.Law.FirearmMinimumOnlyAfterPublication", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerFirearm::RunTest(const FString&)
{
	FPuffersCountyLawVersion Law;
	Law.FineOnlyCapMg = 3500;
	FPuffersCaseFacts Facts;
	Facts.CannabisMg = 1000;
	Facts.bFirearmInIncident = true;
	TestFalse(TEXT("No minimum before the law changes"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).bFirearmOffense);

	Law.FirearmMinimumYears = 5.f;
	const FPuffersCaseOutcome After = FPuffersCountyLawRules::EvaluateCase(Law, Facts);
	TestTrue(TEXT("Firearm offense after"), After.bFirearmOffense);
	TestTrue(TEXT("Court years floored at 5"), FPuffersCountyLawRules::SentenceYears(After, 1.f) >= 5.f);
	Facts.bDocumentedLicensedInventory = true;
	TestTrue(TEXT("Applies to licensed owners too"), FPuffersCountyLawRules::EvaluateCase(Law, Facts).bFirearmOffense);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerReform, "Puffers.Career.Law.ReformPassesThroughJudgeAndPublishes", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerReform::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 3000;
	FString Why;
	TestTrue(TEXT("No civic access yet"), F.Career->Propose(TEXT("REFORM_SIX_POUNDS"), TEXT("Central"), Why).IsNone());

	F.Career->StartOperation(TEXT("OP_SHOP"), TEXT("CANNABIS_SHOP"), TEXT("Central"), NAME_None, FString(), Why);
	F.Career->RunAction(TEXT("InstallAllFixtures:OP_SHOP"), NAME_None);
	TestTrue(TEXT("Shop opens (owner works the counter)"), F.Career->OpenOperation(TEXT("OP_SHOP"), Why));
	FPuffersOperation& Shop = F.Save()->Owner.Operations[0];
	Shop.ProfitableWeeks = F.Config.CivicAccessProfitableWeeks;
	Shop.NetProfit = F.Config.CivicAccessNetProfit;

	const FName Proposal = F.Career->Propose(TEXT("REFORM_SIX_POUNDS"), TEXT("Central"), Why);
	TestFalse(TEXT("Proposal drafted"), Proposal.IsNone());
	TestTrue(TEXT("Support 1"), F.Career->AddProposalSupport(Proposal, TEXT("Workers"), 35));
	TestFalse(TEXT("Each source counts once"), F.Career->AddProposalSupport(Proposal, TEXT("Workers"), 35));
	F.Career->AddProposalSupport(Proposal, TEXT("Neighbors"), 35);
	F.Career->AddProposalSupport(Proposal, TEXT("Testimony"), 35);
	TestTrue(TEXT("Submitted"), F.Career->SubmitForReview(Proposal));
	Same(*this, TEXT("NPC judge approves at threshold"), F.Career->RunNpcJudge(Proposal, Why), EPuffersJudgeDecision::Approve);

	const FPuffersCaseOutcome Before = F.Case(TEXT("CASE_BEFORE"), 100000);
	TestTrue(TEXT("100 g is custody before publication"), Before.PossessionCustodyYears > 0.f);
	F.Advance(25.0);
	const FPuffersCountyLawVersion* Current = FPuffersCountyLawRules::SelectVersion(F.Save()->Law, TEXT("Central"), F.Clock);
	TestTrue(TEXT("Published version raised the cap"), Current && Current->FineOnlyCapMg == FPuffersCountyLawRules::SixPoundsMg);
	Same(*this, TEXT("The earlier case keeps the law at its conduct"), F.Case(TEXT("CASE_BEFORE"), 100000).PossessionCustodyYears, Before.PossessionCustodyYears);
	Same(*this, TEXT("A new case uses the new law"), F.Case(TEXT("CASE_AFTER"), 100000).PossessionCustodyYears, 0.f);
	TestTrue(TEXT("Reform milestone"), F.Career->HasMilestone(EPuffersFirstBuildMilestone::ReformPassed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerBribe, "Puffers.Career.Law.BribesNeverReroll", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerBribe::RunTest(const FString&)
{
	Same(*this, TEXT("Same offer, same roll"), FPuffersCountyLawRules::ResolveBribe(TEXT("OFFER_1"), 0.4f, 0.6f, 400, 400),
		FPuffersCountyLawRules::ResolveBribe(TEXT("OFFER_1"), 0.4f, 0.6f, 400, 400));
	Same(*this, TEXT("Clean officials report a lowball"), FPuffersCountyLawRules::ResolveBribe(TEXT("OFFER_2"), 1.f, 0.f, 10, 400), EPuffersBribeOutcome::Reported);

	FFixture F;
	F.Wallet = 2000;
	const EPuffersBribeOutcome First = F.Career->OfferBribe(TEXT("OFFER_3"), TEXT("NPC_ClerkHollis"), 400, TEXT("Permit"));
	Same(*this, TEXT("Retrying the same offer returns the same answer"), F.Career->OfferBribe(TEXT("OFFER_3"), TEXT("NPC_ClerkHollis"), 400, TEXT("Permit")), First);
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Care and raids

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerCareWindow, "Puffers.Career.Care.SevenDayBoundary", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerCareWindow::RunTest(const FString&)
{
	FFixture F;
	FString Why;
	Same(*this, TEXT("Offline career isn't raidable"), F.Career->GetCareStatus(), EPuffersCareStatus::NotOnline);
	F.Career->ActivateOnlineCareer();
	TestFalse(TEXT("Apartment rented"), F.Career->SubscribeApartment(TEXT("APT_TestYard1"), TEXT("BILL_1"), Why).IsNone());
	F.Career->ClockIn();
	TestTrue(TEXT("Care recorded"), F.Career->CompleteCareActivity(TEXT("CARE_1"), TEXT("EatAndRest"), Why));
	TestFalse(TEXT("The same care op counts once"), F.Career->CompleteCareActivity(TEXT("CARE_1"), TEXT("EatAndRest"), Why));

	F.Clock += FTimespan::FromMinutes(168 * 60 - 1);
	Same(*this, TEXT("Current just before 7 days"), F.Career->GetCareStatus(), EPuffersCareStatus::Current);
	F.Clock += FTimespan::FromMinutes(1);
	Same(*this, TEXT("Exposed at the deadline"), F.Career->GetCareStatus(), EPuffersCareStatus::OverdueExposed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerSelfCover, "Puffers.Career.Care.SelfCoverProtectsAndClaimsPayOnce", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerSelfCover::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 2000;
	FString Why;
	F.Career->ActivateOnlineCareer();
	F.Career->SubscribeApartment(TEXT("APT_TestYard1"), TEXT("BILL_1"), Why);
	TestFalse(TEXT("Self cover bought while current"), F.Career->BuyInsurance(TEXT("SELF_COVER"), NAME_None, NAME_None, true, Why).IsNone());
	F.Clock += FTimespan::FromDays(8);
	Same(*this, TEXT("Overdue but protected"), F.Career->GetCareStatus(), EPuffersCareStatus::OverdueProtected);
	TestFalse(TEXT("Can't raid a protected estate"), F.Career->StartRaid(TEXT("R1"), TEXT("Raider"), Why));

	const FName Policy = F.Career->BuyInsurance(TEXT("TEAM_LIFE"), TEXT("Crewmate"), NAME_None, true, Why);
	TestFalse(TEXT("Team member policy bought with consent"), Policy.IsNone());
	FPuffersLossEvent Loss;
	Loss.EventId = TEXT("DEATH_1");
	Loss.CharacterId = TEXT("Crewmate");
	Loss.At = F.Clock;
	Loss.bPermanentDeath = true;
	Loss.bConfirmed = true;
	const int64 Before = F.Wallet;
	TestTrue(TEXT("Claim pays"), F.Career->FileInsuranceClaim(Policy, Loss, Why));
	TestFalse(TEXT("Claim pays once"), F.Career->FileInsuranceClaim(Policy, Loss, Why));
	Same(*this, TEXT("Benefit paid to the owner"), F.Wallet, Before + 5000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerRaid, "Puffers.Career.Raids.CareClosesAndSuccessSettlesOnce", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerRaid::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 1200;
	FString Why;
	F.Career->ActivateOnlineCareer();
	F.Career->SubscribeApartment(TEXT("APT_TestYard1"), TEXT("BILL_1"), Why);
	F.Clock += FTimespan::FromDays(7.5);

	TestTrue(TEXT("Raid starts on an exposed estate"), F.Career->StartRaid(TEXT("R1"), TEXT("Raider"), Why));
	TestFalse(TEXT("One raid at a time"), F.Career->StartRaid(TEXT("R2"), TEXT("Raider2"), Why));
	F.Career->ClockIn();
	F.Career->CompleteCareActivity(TEXT("CARE_LATE"), TEXT("EatAndRest"), Why);
	TestFalse(TEXT("Care before success closes the raid"), F.Career->SettleRaidSuccess(TEXT("R1"), Why));
	Same(*this, TEXT("Wallet untouched"), F.Wallet, 1200ll);

	// The billing for the apartment continues, so it's still a qualifying residence next week.
	F.Career->ApplyBillingEvent(F.Save()->Housing.Tenancies[0].EntitlementId, TEXT("BILL_2"), true, false);
	F.Clock += FTimespan::FromDays(7.5);
	TestTrue(TEXT("A new raid"), F.Career->StartRaid(TEXT("R3"), TEXT("Raider"), Why));
	TestTrue(TEXT("Success settles"), F.Career->SettleRaidSuccess(TEXT("R3"), Why));
	Same(*this, TEXT("The whole uninsured estate moves, cash included"), F.Wallet, 0ll);
	TestFalse(TEXT("Repeat callbacks never pay twice"), F.Career->SettleRaidSuccess(TEXT("R3"), Why));
	TestTrue(TEXT("Settlement published"), F.Published(UPuffersCareerSubsystem::EventRaidSettled));
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Housing

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerApartment, "Puffers.Career.Housing.ApartmentLocksReinstatesAndReclaims", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerApartment::RunTest(const FString&)
{
	FFixture F;
	FString Why;
	const FName Tenancy = F.Career->SubscribeApartment(TEXT("APT_Downtown2"), TEXT("BILL_1"), Why);
	TestTrue(TEXT("Enter while current"), F.Career->CanEnterApartment(Tenancy));
	TestTrue(TEXT("Outskirts plots aren't monthly rentals"), F.Career->SubscribeApartment(TEXT("PLOT_Frontier_001"), TEXT("BILL_X"), Why).IsNone());

	F.Advance(24.0 * 31);
	TestFalse(TEXT("Locked after the missed renewal"), F.Career->CanEnterApartment(Tenancy));
	const FName Entitlement = F.Save()->Housing.Tenancies[0].EntitlementId;
	TestTrue(TEXT("Late payment applies"), F.Career->ApplyBillingEvent(Entitlement, TEXT("BILL_2"), true, false));
	TestFalse(TEXT("Billing repeats are ignored"), F.Career->ApplyBillingEvent(Entitlement, TEXT("BILL_2"), true, false));
	F.Advance(1.0);
	TestTrue(TEXT("Reinstated with contents intact"), F.Career->CanEnterApartment(Tenancy));

	F.Advance(24.0 * 31);
	F.Advance(24.0 * 31);
	Same(*this, TEXT("Reclaimed after the grace period"), F.Save()->Housing.Tenancies[0].State, EPuffersTenancyState::Reclaimed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerVacancyRace, "Puffers.Career.Housing.OneApplicantGetsTheUnit", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerVacancyRace::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 5000;
	FString Why;
	const FName First = F.Career->ApplyForVacancy(TEXT("STORE_CigarLounge"), TEXT("NIGHTCLUB"), TEXT("Haze Room"), Why);
	TestFalse(TEXT("First application approved"), First.IsNone());
	TestTrue(TEXT("Wrong use refused"), F.Career->ApplyForVacancy(TEXT("STORE_Gym"), TEXT("NIGHTCLUB"), TEXT("Nope"), Why).IsNone());
	TestTrue(TEXT("A reserved unit can't be taken twice"), F.Career->ApplyForVacancy(TEXT("STORE_CigarLounge"), TEXT("RESTAURANT"), TEXT("Second"), Why).IsNone());
	TestFalse(TEXT("Possession opens the fit-out"), F.Career->TakePossession(First, Why).IsNone());
	const int64 After = F.Wallet;
	F.Career->TakePossession(First, Why);
	Same(*this, TEXT("Upfront charged once"), F.Wallet, After);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerAsBuilt, "Puffers.Career.Housing.AsBuiltSalePaysLienAndKeepsTenants", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerAsBuilt::RunTest(const FString&)
{
	FFixture F([](FFixture& Fx)
	{
		// Keep the loan current through the lapse so the sale, not a repossession, settles it.
		for (FPuffersLenderDefinition& Lender : Fx.Config.Lenders)
		{
			Lender.DaysBetweenInstallments = 90;
		}
	});
	F.Wallet = 6000;
	FString Why;
	TestTrue(TEXT("Plot bought"), F.Career->BuyPlot(TEXT("PLOT_Frontier_001"), TEXT("BILL_P1"), Why));
	TestTrue(TEXT("Home built"), F.Career->AddPlotBuilding(TEXT("PLOT_Frontier_001"), TEXT("BLD_Home"), TEXT("Home"), true, Why));
	TestFalse(TEXT("Leased to a player"), F.Career->LeaseToPlayer(TEXT("PLOT_Frontier_001"), TEXT("BLD_Home"), TEXT("Tenant"), 200, 200, 7, Why).IsNone());

	FPuffersCollateral Home;
	Home.Kind = EPuffersCollateralKind::Home;
	Home.AssetId = TEXT("PLOT_Frontier_001");
	Home.Value = 8000;
	const FName Loan = F.Career->QuoteLoan(TEXT("PUFF_CREDIT"), Home, 0, Why);
	TestFalse(TEXT("Home loan quoted"), Loan.IsNone());
	TestTrue(TEXT("Loan funded"), F.Career->AcceptLoan(Loan, Why));
	TestTrue(TEXT("Plot liened"), F.Career->IsAssetLiened(TEXT("PLOT_Frontier_001")));

	F.Advance(24.0 * 31);
	const FPuffersPlotOwnership* Plot = F.Save()->Housing.Plots.FindByPredicate([](const FPuffersPlotOwnership& P) { return P.PlotId == TEXT("PLOT_Frontier_001"); });
	TestTrue(TEXT("Lapsed plot with a home is listed as built"), Plot && Plot->State == EPuffersPlotState::ListedAsBuilt);
	TestFalse(TEXT("A stale listing version is refused"), F.Career->BuyPlotAsBuilt(TEXT("PLOT_Frontier_001"), TEXT("Buyer"), { 999 }, Why));
	TestTrue(TEXT("Sale settles"), F.Career->BuyPlotAsBuilt(TEXT("PLOT_Frontier_001"), TEXT("Buyer"), Plot->ListedBuildVersions, Why));
	TestFalse(TEXT("Lien paid off first"), F.Career->IsAssetLiened(TEXT("PLOT_Frontier_001")));
	Same(*this, TEXT("New owner"), Plot->OwnerId, FName(TEXT("Buyer")));
	TestTrue(TEXT("The tenant keeps the lease, now with the buyer"), Plot->Leases.Num() == 1 && Plot->Leases[0].LandlordId == TEXT("Buyer"));
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Loans and payroll

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerLoans, "Puffers.Career.Loans.LiensRepaymentAndRepossession", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerLoans::RunTest(const FString&)
{
	FFixture F;
	FString Why;
	FPuffersCollateral Car;
	Car.Kind = EPuffersCollateralKind::Vehicle;
	Car.AssetId = TEXT("VEH_Sedan");
	Car.Value = 4000;
	Car.Condition = 0.5f;
	const FName Loan = F.Career->QuoteLoan(TEXT("PUFF_CREDIT"), Car, 99999, Why);
	TestFalse(TEXT("Quoted"), Loan.IsNone());
	TestTrue(TEXT("Accepted"), F.Career->AcceptLoan(Loan, Why));
	Same(*this, TEXT("Principal capped by value x condition x LTV"), F.Wallet, 1000ll);
	TestTrue(TEXT("A pledged asset can't secure a second loan"), F.Career->QuoteLoan(TEXT("PAWN_EAST"), Car, 0, Why).IsNone());

	const FPuffersLoan& Funded = F.Save()->Workforce.Loans[0];
	F.Wallet += 1000;
	const int64 Due = Funded.Outstanding();
	Same(*this, TEXT("Repay in full"), F.Career->RepayLoan(Loan, TEXT("PAY_1"), Due + 500), Due);
	Same(*this, TEXT("A repeated payment id is ignored"), F.Career->RepayLoan(Loan, TEXT("PAY_1"), 10), 0ll);
	TestFalse(TEXT("Lien released"), F.Career->IsAssetLiened(TEXT("VEH_Sedan")));

	const FName Second = F.Career->QuoteLoan(TEXT("PAWN_EAST"), Car, 0, Why);
	F.Career->AcceptLoan(Second, Why);
	F.Advance(24.0 * 7 + 1);
	TestTrue(TEXT("Not repossessed inside the grace period"), F.VehiclesTaken.Num() == 0);
	F.Advance(24.0 * 3);
	TestTrue(TEXT("Repossessed after the grace period"), F.VehiclesTaken.Contains(FName(TEXT("VEH_Sedan"))));
	Same(*this, TEXT("Loan closed by repossession"), F.Save()->Workforce.Loans[1].State, EPuffersLoanState::Repossessed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerPayroll, "Puffers.Career.Payroll.HiringFloorPayDayAndUnpaidWages", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerPayroll::RunTest(const FString&)
{
	FFixture F;
	F.Wallet = 900;
	FString Why;
	F.Career->StartOperation(TEXT("OP_BODEGA"), TEXT("BODEGA"), TEXT("Central"), NAME_None, FString(), Why);
	Same(*this, TEXT("Entry paid"), F.Wallet, 0ll);
	TestTrue(TEXT("Hiring floor blocks a broke owner"), F.Career->OfferJob(TEXT("OP_BODEGA"), TEXT("NPC_Clerk"), true, TEXT("Clerk"), 100, 5, Why).IsNone());

	F.Wallet = 600;
	const FName Clerk = F.Career->OfferJob(TEXT("OP_BODEGA"), TEXT("NPC_Clerk"), true, TEXT("Clerk"), 100, 5, Why);
	TestFalse(TEXT("AI clerk hired once the floor is met"), Clerk.IsNone());

	auto WorkShift = [&F](FName Employment)
	{
		const FName Shift = F.Career->PublishShift(TEXT("OP_BODEGA"), Employment, F.Clock, F.Clock + FTimespan::FromHours(4), TEXT("Register"));
		F.Career->CheckInShift(Shift);
		F.Career->CompleteShift(Shift, 3);
		F.Clock += FTimespan::FromHours(5);
	};
	WorkShift(Clerk);
	const FName Run1 = F.Career->RunPayroll(TEXT("OP_BODEGA"), F.Clock);
	Same(*this, TEXT("Wages paid"), F.Wallet, 500ll);
	Same(*this, TEXT("Same cutoff runs once"), F.Career->RunPayroll(TEXT("OP_BODEGA"), F.Clock), Run1);
	Same(*this, TEXT("Still paid once"), F.Wallet, 500ll);

	F.Wallet = 50;
	WorkShift(Clerk);
	F.Career->RunPayroll(TEXT("OP_BODEGA"), F.Clock);
	TestTrue(TEXT("Unpaid alert"), F.AnyNotice(TEXT("Your boss can't pay you this week")));
	TestTrue(TEXT("Unpaid event"), F.Published(UPuffersCareerSubsystem::EventPayrollUnpaid));
	const TArray<FPuffersEmployment> Staff = F.Career->GetEmployment(TEXT("OP_BODEGA"));
	Same(*this, TEXT("Arrears recorded"), Staff[0].Arrears, 100ll);
	Same(*this, TEXT("The AI clerk decided after losing trust"), Staff[0].LastChoice, FPuffersWorkforceRules::DecideAI(Staff[0]));

	F.Wallet = 5000;
	TestTrue(TEXT("No hiring while wages are owed"), F.Career->OfferJob(TEXT("OP_BODEGA"), TEXT("NPC_Two"), true, TEXT("Clerk"), 100, 5, Why).IsNone());
	Same(*this, TEXT("Back wages settled"), F.Career->SettleArrears(TEXT("OP_BODEGA")), 100ll);
	TestFalse(TEXT("Hiring works again"), F.Career->OfferJob(TEXT("OP_BODEGA"), TEXT("NPC_Two"), true, TEXT("Clerk"), 100, 5, Why).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerRobTheBoss, "Puffers.Career.Payroll.WorkerChoicesAfterUnpaidRuns", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerRobTheBoss::RunTest(const FString&)
{
	FPuffersEmployment Loyal;
	Loyal.Trust = 80;
	Loyal.UnpaidRuns = 1;
	Same(*this, TEXT("A trusting worker waits"), FPuffersWorkforceRules::DecideAI(Loyal), EPuffersUnpaidChoice::Wait);
	FPuffersEmployment Upset = Loyal;
	Upset.Trust = 40;
	Same(*this, TEXT("A worried worker quits"), FPuffersWorkforceRules::DecideAI(Upset), EPuffersUnpaidChoice::Quit);
	FPuffersEmployment Furious = Upset;
	Furious.Trust = 10;
	Furious.UnpaidRuns = 2;
	Same(*this, TEXT("A furious worker robs the boss"), FPuffersWorkforceRules::DecideAI(Furious), EPuffersUnpaidChoice::RobTheBoss);
	Same(*this, TEXT("Alert text"), FPuffersWorkforceRules::UnpaidAlert(250), FString(TEXT("Your boss can't pay you this week. You're owed 250 Puff Cash.")));
	return true;
}

// ------------------------------------------------------------------------------------------------------------- Hooks and modes

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerHooks, "Puffers.Career.Hooks.FirstOperationMissionsDriveTheCareer", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerHooks::RunTest(const FString&)
{
	FFixture F;
	F.Career->HandleEvent(TEXT("ObjectiveDone"), TEXT("FO01_ClockOut"));
	Same(*this, TEXT("A shift pays"), F.Wallet, 220ll);
	F.Career->HandleEvent(TEXT("ObjectiveDone"), TEXT("FO01_ClockOut"));
	Same(*this, TEXT("A replayed shift pays again"), F.Wallet, 440ll);
	TestTrue(TEXT("Paid shift milestone"), F.Career->HasMilestone(EPuffersFirstBuildMilestone::PaidShift));

	F.Wallet = 800;
	F.Career->HandleEvent(TEXT("ObjectiveDone"), TEXT("FO02_Street"));
	F.Career->HandleEvent(TEXT("ObjectiveDone"), TEXT("FO02_Fixtures"));
	F.Career->HandleEvent(TEXT("ObjectiveDone"), TEXT("FO02_Open"));
	Same(*this, TEXT("Street lane commits"), F.Career->GetRoute(), EPuffersCareerRoute::StreetDualEligible);
	TestTrue(TEXT("Corner open"), F.Save()->Owner.Operations.Num() == 1 && F.Save()->Owner.Operations[0].bOpen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuffersCareerOffline, "Puffers.Career.Modes.OfflineIsStoryOnly", PUFFERS_CAREER_TEST_FLAGS)
bool FPuffersCareerOffline::RunTest(const FString&)
{
	FFixture F;
	F.Career->SharedWorldOverride = []() { return false; };
	F.Wallet = 5000;
	FString Why;
	TestTrue(TEXT("No operations offline"), F.Career->StartOperation(TEXT("OP_X"), TEXT("BODEGA"), TEXT("Central"), NAME_None, FString(), Why).IsNone());
	TestTrue(TEXT("No apartments offline"), F.Career->SubscribeApartment(TEXT("APT_TestYard1"), TEXT("B"), Why).IsNone());
	TestFalse(TEXT("No online career offline"), F.Career->ActivateOnlineCareer());
	TestTrue(TEXT("Story-side law still works"), !F.Career->DescribeCurrentLaw(TEXT("Central")).IsEmpty());
	return true;
}

#endif
