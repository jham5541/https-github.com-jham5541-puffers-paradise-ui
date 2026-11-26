// Data for the career systems, loaded from Content/Data/Career/*.json. Every tuning number lives here, not in code.

#pragma once

#include "CoreMinimal.h"
#include "Career/PuffersCareerTypes.h"
#include "Career/PuffersCountyLawTypes.h"
#include "Career/PuffersInsiderTypes.h"
#include "Career/PuffersEstateTypes.h"
#include "Career/PuffersHousingTypes.h"
#include "Career/PuffersWorkforceTypes.h"
#include "PuffersCareerCatalog.generated.h"

// Career.json
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerConfig
{
	GENERATED_BODY()

	// Jayde, Nov 26 2026: 7 days.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float CareWindowHours = 168.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ApartmentGraceDays = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 BillingPeriodDays = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VacancyReservationHours = 48.f;

	// Hiring floor: a business needs at least this much available, or a week of projected payroll if higher.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 HiringMinimum = 500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 PayrollDays = 7;

	// Raid winner's carry capacity; the rest goes to claim storage.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RaidCarrySlots = 30;

	// Capital targets shown on the first-paycheck screen.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 LicensedCapitalTarget = 1500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 StreetCapitalTarget = 800;

	// Licensed civic access (reform sponsorship) needs this many profitable weeks and this much net cannabis profit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 CivicAccessProfitableWeeks = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 CivicAccessNetProfit = 2000;

	// Missions that grant regulated ownership go through the license gate (LC05 opens a shop).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> RegulatedOwnershipMissions;

	// The free-to-enter job offered to a career reset to zero assets.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName RecoveryJobId = TEXT("SHOP_STAFF");

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersCareActivity> CareActivities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersCareerPhase> Phases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersBusinessArchetype> Archetypes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersInsuranceProduct> InsuranceProducts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersLenderDefinition> Lenders;
};

// CountyLaws.json
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCountyLawCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersCountyDefinition> Counties;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersProposalTemplate> Proposals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FPuffersCustodyScale CustodyScale;

	// The county the Test Yard / Story Lot sits in when the map lookup finds nothing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName DefaultCounty = TEXT("Central");
};

// Insiders.json
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsiderCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersInsiderDefinition> Insiders;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersInsiderRequestDefinition> Requests;

	// Bribe "price" by official id (what they'd consider a fair offer).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FName, int32> BribePrices;
};

// Housing.json
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersHousingCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersApartmentListing> Apartments;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersCommercialVacancy> Vacancies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersPlotListing> Plots;
};

// Hooks.json: what a mission objective or event does to the career systems. Missions stay pure data; the hooks
// translate "you finished this objective" into a wage, a commitment, a finding, a law change...
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerHook
{
	GENERATED_BODY()

	// "ObjectiveDone" (Context = objective id), "MissionCompleted" (Context = mission id), or any game event name.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Event;

	// None matches any context.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Context;

	// "Verb:arg:arg" actions, run in order. See Docs/CareerSystems.md for the list.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FString> Actions;

	// Run at most once per save (otherwise once per completion).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bOnce = false;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareerHookCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FPuffersCareerHook> Hooks;
};
