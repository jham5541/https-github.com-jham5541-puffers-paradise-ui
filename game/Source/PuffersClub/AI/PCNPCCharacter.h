#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "PCNPCCharacter.generated.h"

class UAbilitySystemComponent;
class UPCAttributeSet;

/**
 * Base ambient NPC (pedestrians, workers, mission enemies). Full-actor NPCs
 * only exist near players; distant population is Mass Entity (Phase 2+).
 */
UCLASS()
class PUFFERSCLUB_API APCNPCCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	APCNPCCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 0..1 — biases flee vs. confront when witnessing crime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality", meta = (ClampMin = "0", ClampMax = "1"))
	float Fear = 0.5f;

	/** 0..1 — chance of reporting a witnessed crime to police. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Personality", meta = (ClampMin = "0", ClampMax = "1"))
	float Civility = 0.8f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UPCAttributeSet> AttributeSet;
};
