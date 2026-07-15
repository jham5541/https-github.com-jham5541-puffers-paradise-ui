#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PCInteractableInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UPCInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything a player can interact with: vehicle doors, building entrances,
 * shop counters, mission givers. Implement in C++ or Blueprint; the server
 * re-checks range and permissions before honoring the interaction.
 */
class PUFFERSCLUB_API IPCInteractableInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(AActor* InteractingActor);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractionPrompt() const;
};
