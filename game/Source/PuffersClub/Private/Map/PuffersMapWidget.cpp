#include "Map/PuffersMapWidget.h"

#include "Map/PuffersMapSubsystem.h"
#include "Map/SPuffersMapView.h"
#include "Engine/GameInstance.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UPuffersMapWidget::RebuildWidget()
{
	const UGameInstance* GI = GetGameInstance();
	UPuffersMapSubsystem* Map = GI ? GI->GetSubsystem<UPuffersMapSubsystem>() : nullptr;
	if (!Map)
	{
		// Designer preview has no game instance.
		return SNew(STextBlock).Text(NSLOCTEXT("Puffers", "MapPreview", "Puffers Map (shows in game)"));
	}
	View = SNew(SPuffersMapView).MapSubsystem(Map).Mode(bMinimap ? SPuffersMapView::EMode::Minimap : SPuffersMapView::EMode::Embedded);
	return View.ToSharedRef();
}

void UPuffersMapWidget::FocusOn(FVector2D MapPoint, float Zoom)
{
	if (View.IsValid())
	{
		View->FocusOn(MapPoint, Zoom);
	}
}

void UPuffersMapWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	View.Reset();
}
