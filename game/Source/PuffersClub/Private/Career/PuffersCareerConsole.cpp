// Playtest console commands for the career systems (Test Yard, development builds).
//
//   puffers.Career.Status              route, phase, standing, care, wallet accounts, recent notices
//   puffers.Career.Checklist           the "Earn your first operation" checklist
//   puffers.Career.Do <Action>         runs any hook action, e.g. puffers.Career.Do StartOperation:CANNABIS_SHOP:OP_TEST:Central
//   puffers.Career.Law <County>        the law in force in a county
//   puffers.Career.Manifest            what an inactivity raid would take right now
//   puffers.Career.Reload              reloads Content/Data/Career

#include "Career/PuffersCareerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "PuffersClub.h"

#if !UE_BUILD_SHIPPING

namespace
{
	UPuffersCareerSubsystem* CareerFor(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UPuffersCareerSubsystem>() : nullptr;
	}

	void Print(const FString& Line)
	{
		UE_LOG(LogPuffers, Display, TEXT("%s"), *Line);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdStatus(TEXT("puffers.Career.Status"), TEXT("Career route, phase, care and wallet."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			UPuffersCareerSubsystem* Career = CareerFor(World);
			if (!Career)
			{
				return;
			}
			for (const FString& Line : Career->GetCareerSummary())
			{
				Print(Line);
			}
			Print(FString::Printf(TEXT("Shared world: %s"), Career->IsSharedWorld() ? TEXT("yes") : TEXT("no (story only)")));
			Print(FString::Printf(TEXT("Care: %s, deadline %s UTC"), *UEnum::GetValueAsString(Career->GetCareStatus()),
				*Career->GetCareDeadline().ToString(TEXT("%Y-%m-%d %H:%M"))));
			if (const UPuffersCareerSaveGame* Save = Career->GetSave())
			{
				for (const FName& Account : Save->Ledger.GetOwnedAccounts())
				{
					Print(FString::Printf(TEXT("  %s: %lld (available %lld)"), *Account.ToString(), Career->GetBalance(Account), Career->GetAvailable(Account)));
				}
			}
			for (const FString& Notice : Career->GetNotices(5))
			{
				Print(FString::Printf(TEXT("  > %s"), *Notice));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdChecklist(TEXT("puffers.Career.Checklist"), TEXT("The first-operation checklist."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UPuffersCareerSubsystem* Career = CareerFor(World))
			{
				for (const FString& Line : Career->GetFirstBuildChecklist())
				{
					Print(Line);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDo(TEXT("puffers.Career.Do"), TEXT("Runs a career hook action (Verb:arg:arg). See Docs/CareerSystems.md."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UPuffersCareerSubsystem* Career = CareerFor(World);
			if (!Career || Args.Num() == 0)
			{
				return;
			}
			const FString Action = FString::Join(Args, TEXT(" "));
			const bool bOk = Career->RunAction(Action, TEXT("Console"));
			Print(FString::Printf(TEXT("%s -> %s"), *Action, bOk ? TEXT("ok") : TEXT("refused")));
			for (const FString& Notice : Career->GetNotices(2))
			{
				Print(FString::Printf(TEXT("  > %s"), *Notice));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdLaw(TEXT("puffers.Career.Law"), TEXT("Describes the law in force in a county."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UPuffersCareerSubsystem* Career = CareerFor(World))
			{
				Print(Career->DescribeCurrentLaw(Args.Num() > 0 ? FName(*Args[0]) : NAME_None));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdManifest(TEXT("puffers.Career.Manifest"), TEXT("What an inactivity raid would take right now."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UPuffersCareerSubsystem* Career = CareerFor(World))
			{
				for (const FPuffersEstateLine& Line : Career->BuildEstateManifest())
				{
					Print(FString::Printf(TEXT("  %s %s x%lld %s"), *UEnum::GetValueAsString(Line.Kind), *Line.AssetId.ToString(), static_cast<int64>(Line.Quantity),
						Line.Transfers() ? TEXT("TRANSFERS") : *UEnum::GetValueAsString(Line.Exclusion)));
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdReload(TEXT("puffers.Career.Reload"), TEXT("Reloads Content/Data/Career."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UPuffersCareerSubsystem* Career = CareerFor(World))
			{
				Print(FString::Printf(TEXT("Career data reloaded (%d entries)."), Career->ReloadData()));
			}
		}));
}

#endif
