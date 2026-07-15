#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCCharacterPreset.generated.h"

class USkeletalMesh;
class UTexture2D;

/**
 * A selectable character preset. Presets are the MVP form of character
 * creation: every preset shares the same base skeleton so one animation set
 * drives all of them, and they later become starting points inside the full
 * morph/clothing/tattoo creator (docs/design/07-vehicles-and-characters.md).
 *
 * Art spec per preset: rigged skeletal mesh (FBX/glTF) on the shared project
 * skeleton — NOT static OBJ geometry — with base color/normal/ORM textures.
 */
UCLASS(BlueprintType)
class PUFFERSCLUB_API UPCCharacterPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	FName PresetId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	FText Description;

	/** Must be rigged to the shared project skeleton. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	TSubclassOf<UAnimInstance> AnimClass;

	/** Shown in the selection UI. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Preset")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("CharacterPreset"), PresetId);
	}
};
