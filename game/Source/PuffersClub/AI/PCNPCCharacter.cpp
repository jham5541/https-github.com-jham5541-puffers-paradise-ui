#include "AI/PCNPCCharacter.h"

#include "AbilitySystemComponent.h"
#include "Abilities/PCAttributeSet.h"
#include "AI/PCNPCController.h"

APCNPCCharacter::APCNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	AIControllerClass = APCNPCController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UPCAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* APCNPCCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
