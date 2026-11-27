// UMG wrapper so designers can drop the map (or a minimap) into any widget Blueprint: phone app, property browser, HUD.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "PuffersMapWidget.generated.h"

class SPuffersMapView;

UCLASS(meta = (DisplayName = "Puffers Map"))
class PUFFERSCLUB_API UPuffersMapWidget : public UWidget
{
	GENERATED_BODY()

public:
	// Minimap behavior (follows the player, no input) or an interactive embedded map.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	bool bMinimap = false;

	UFUNCTION(BlueprintCallable, Category = "Map")
	void FocusOn(FVector2D MapPoint, float Zoom);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override { return NSLOCTEXT("Puffers", "Puffers", "Puffers"); }
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SPuffersMapView> View;
};
