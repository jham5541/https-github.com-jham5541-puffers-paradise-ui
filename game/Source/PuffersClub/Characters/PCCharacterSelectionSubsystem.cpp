#include "Characters/PCCharacterSelectionSubsystem.h"

#include "Characters/PCCharacterPreset.h"
#include "Engine/AssetManager.h"

TArray<UPCCharacterPreset*> UPCCharacterSelectionSubsystem::LoadAllPresets() const
{
	TArray<UPCCharacterPreset*> Presets;

	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> Ids;
	Manager.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("CharacterPreset")), Ids);

	for (const FPrimaryAssetId& Id : Ids)
	{
		// Synchronous load is acceptable at the selection screen; switch to
		// async batches once the roster grows.
		if (UPCCharacterPreset* Preset = Cast<UPCCharacterPreset>(Manager.GetPrimaryAssetObject(Id)))
		{
			Presets.Add(Preset);
		}
		else if (UObject* Loaded = Manager.GetStreamableManager().LoadSynchronous(Manager.GetPrimaryAssetPath(Id)))
		{
			if (UPCCharacterPreset* LoadedPreset = Cast<UPCCharacterPreset>(Loaded))
			{
				Presets.Add(LoadedPreset);
			}
		}
	}

	return Presets;
}
