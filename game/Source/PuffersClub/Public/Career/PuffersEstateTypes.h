// Online character care, life insurance, inactivity raids and estate transfer
// (handoff Rev 12, "Character care and life insurance" and "Online home raids and estate transfer").
//
// Jayde's decisions (Nov 26, 2026) override the doc:
//  * The care window is 7 days (168 hours), not 48. It lives in Career.json, so it can be tuned.
//  * A successful raid can take the WHOLE eligible estate, owned homes and their contents included.
//    Self life insurance is the protection.
// Online only: offline story saves never accrue care exposure while the game is closed.

#pragma once

#include "CoreMinimal.h"
#include "PuffersEstateTypes.generated.h"

UENUM(BlueprintType)
enum class EPuffersCareStatus : uint8
{
	NotOnline,         // offline story save or no online career yet
	Current,
	OverdueProtected,  // past the window with active self coverage: no raid
	OverdueExposed     // past the window, uninsured, with a qualifying residence: raidable
};

UENUM(BlueprintType)
enum class EPuffersInsuranceKind : uint8
{
	Self,               // includes estate protection against the inactivity raid
	TeamMember,
	SignificantOther
};

UENUM(BlueprintType)
enum class EPuffersRaidState : uint8
{
	Open,
	Succeeded,
	Failed,
	Abandoned,
	ClosedByCare    // the owner did their care before success committed
};

UENUM(BlueprintType)
enum class EPuffersEstateAssetKind : uint8
{
	Cash,
	Item,
	Vehicle,
	Property,
	BusinessInterest
};

// Why an asset stays put in a raid.
UENUM(BlueprintType)
enum class EPuffersEstateExclusion : uint8
{
	None,
	NonTransferable,       // account-bound rewards, soulbound passport
	RealValueLinked,       // anything linked to real value or an external entitlement
	ThirdPartyOwned,
	PaidTenancy,           // the apartment subscription itself (its loot can go)
	RentReclaimQuarantine, // locked in an overdue apartment
	PlotFrozenForNonpayment,
	Liened                 // moves subject to the creditor's lien, never free of it
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersCareActivity
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	// Can be done from custody (custody routine) or a public place (when the apartment is locked).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bAvailableInCustody = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bAvailableInPublic = true;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsuranceProduct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ProductId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EPuffersInsuranceKind Kind = EPuffersInsuranceKind::Self;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Premium = 500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TermDays = 30;

	// Puff Cash paid to the beneficiary on a covered permanent death (team member / partner).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Benefit = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Insurer = TEXT("Puffington Mutual");
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersInsurancePolicy
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PolicyId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ProductId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersInsuranceKind Kind = EPuffersInsuranceKind::Self;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName InsuredId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PolicyholderId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName BeneficiaryId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StartsAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime EndsAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Premium = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 Benefit = 0;

	// The insured consented (another human player) or accepted (an eligible NPC).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bConsented = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bClaimPaid = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName PremiumTx;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ClaimTx;

	bool IsActiveAt(const FDateTime& At) const { return bConsented && At >= StartsAt && At < EndsAt; }
};

// A loss event a team-member or partner policy might pay on.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersLossEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FName EventId;

	UPROPERTY(BlueprintReadWrite)
	FName CharacterId;

	UPROPERTY(BlueprintReadWrite)
	FDateTime At;

	// Only a confirmed permanent in-game death pays.
	UPROPERTY(BlueprintReadWrite)
	bool bPermanentDeath = false;

	UPROPERTY(BlueprintReadWrite)
	bool bConfirmed = false;

	UPROPERTY(BlueprintReadWrite)
	bool bScriptedOrPractice = false;

	// Who caused it. The beneficiary causing it on purpose voids the claim.
	UPROPERTY(BlueprintReadWrite)
	FName CausedBy;

	UPROPERTY(BlueprintReadWrite)
	bool bIntentional = false;
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEstateLine
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersEstateAssetKind Kind = EPuffersEstateAssetKind::Item;

	// Account name, item id, vehicle id, property id or operation id.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName AssetId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	int64 Quantity = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersEstateExclusion Exclusion = EPuffersEstateExclusion::None;

	// For liened assets: the loan that still holds it.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName LienLoanId;

	// Shared assets transfer only the owner's interest (0..1).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float OwnershipShare = 1.f;

	// Items over the winner's carry capacity go to claim storage.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bToClaimStorage = false;

	bool Transfers() const { return Exclusion == EPuffersEstateExclusion::None || Exclusion == EPuffersEstateExclusion::Liened; }
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEstateTransfer
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TransferId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName RaidId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FromOwner;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ToOwner;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime At;

	// Every balance, item and title, with what happened to it.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersEstateLine> Lines;

	int64 TotalCashMoved() const
	{
		int64 Total = 0;
		for (const FPuffersEstateLine& Line : Lines)
		{
			if (Line.Kind == EPuffersEstateAssetKind::Cash && Line.Transfers())
			{
				Total += Line.Quantity;
			}
		}
		return Total;
	}
};

USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersRaid
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName RaidId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName RaiderId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName ResidenceId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	EPuffersRaidState State = EPuffersRaidState::Open;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime StartedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime EndedAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName TransferId;
};

// Everything care, insurance and raids persist for one career owner.
USTRUCT(BlueprintType)
struct PUFFERSCLUB_API FPuffersEstateState
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bOnlineCareerActive = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime ActivatedAt;

	// Server-stamped.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime LastCareAt;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	bool bClockedIn = false;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime ClockedInAt;

	// Care receipts already recorded (op ids), so a retried completion can't double-count.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> CareReceipts;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersInsurancePolicy> Policies;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersRaid> Raids;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FPuffersEstateTransfer> Transfers;

	// Claims already settled (loss event ids).
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FName> SettledLossEvents;

	// Times this career was reset to zero assets by a raid.
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 EstateResets = 0;
};
