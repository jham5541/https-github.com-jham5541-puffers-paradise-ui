#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "PCNPCController.generated.h"

class UAISenseConfig_Sight;

/**
 * AI controller for ambient NPCs. Sight perception makes NPCs crime
 * witnesses: seeing an armed/violent actor feeds that actor's heat via the
 * witness multiplier (docs/design/06-combat-and-heat.md). Behavior itself
 * (flee, call police, cower) runs in behavior trees assigned per archetype.
 */
UCLASS()
class PUFFERSCLUB_API APCNPCController : public AAIController
{
	GENERATED_BODY()

public:
	APCNPCController();

protected:
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UPROPERTY(VisibleAnywhere, Category = "Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;
};
