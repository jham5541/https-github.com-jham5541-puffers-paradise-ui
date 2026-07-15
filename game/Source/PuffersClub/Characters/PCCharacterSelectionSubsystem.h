#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PCCharacterSelectionSubsystem.generated.h"

class UPCCharacterPreset;

/**
 * Holds the locally selected character preset between the selection screen
 * and spawn. The selection travels to the server on join (login options /
 * player state handshake); the server resolves and validates the preset id
 * before applying the mesh — clients never dictate assets directly.
 */
UCLASS()
class PUFFERSCLUB_API UPCCharacterSelectionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Character Selection")
	void SetSelectedPreset(UPCCharacterPreset* Preset) { SelectedPreset = Preset; }

	UFUNCTION(BlueprintPure, Category = "Character Selection")
	UPCCharacterPreset* GetSelectedPreset() const { return SelectedPreset; }

	/** All presets registered with the asset manager (type 'CharacterPreset'). */
	UFUNCTION(BlueprintCallable, Category = "Character Selection")
	TArray<UPCCharacterPreset*> LoadAllPresets() const;

protected:
	UPROPERTY()
	TObjectPtr<UPCCharacterPreset> SelectedPreset;
};
