#include "AI/PCNPCController.h"

#include "AI/PCNPCCharacter.h"
#include "Heat/PCHeatComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "PuffersClub.h"

APCNPCController::APCNPCController()
{
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 2500.f;
	SightConfig->LoseSightRadius = 3000.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());
}

void APCNPCController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (PerceptionComponent)
	{
		PerceptionComponent->OnTargetPerceptionUpdated.AddUniqueDynamic(
			this, &APCNPCController::OnTargetPerceptionUpdated);
	}
}

void APCNPCController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority() || !Actor || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	// If the sensed actor is carrying active heat, this NPC is now a witness:
	// civility decides whether they report, fear feeds the behavior tree.
	if (UPCHeatComponent* Heat = Actor->FindComponentByClass<UPCHeatComponent>())
	{
		if (Heat->GetHeatTier() != EPCHeatTier::None)
		{
			const APCNPCCharacter* NPC = Cast<APCNPCCharacter>(GetPawn());
			const float Civility = NPC ? NPC->Civility : 0.5f;

			if (FMath::FRand() <= Civility)
			{
				Heat->ReportCrime(/*BaseHeat*/ 2.f, /*WitnessMultiplier*/ 1.f);
				UE_LOG(LogPuffersClub, Verbose, TEXT("%s witnessed and reported %s"),
					*GetNameSafe(GetPawn()), *GetNameSafe(Actor));
			}
		}
	}
}
