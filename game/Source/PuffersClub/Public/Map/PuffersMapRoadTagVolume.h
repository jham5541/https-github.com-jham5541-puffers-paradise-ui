// Place in any level to name roads or force bridge / tunnel / one-way on the roads that pass through the box.
// The surveyor applies these after its automatic detection, so they always win.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Map/PuffersMapTypes.h"
#include "PuffersMapRoadTagVolume.generated.h"

class UBoxComponent;

UCLASS(meta = (DisplayName = "Puffers Map Road Tag"))
class PUFFERSCLUB_API APuffersMapRoadTagVolume : public AActor
{
	GENERATED_BODY()

public:
	APuffersMapRoadTagVolume();

	// Name given to every road whose middle is inside the box (e.g. "Kush Ave").
	UPROPERTY(EditAnywhere, Category = "Map")
	FString RoadName;

	UPROPERTY(EditAnywhere, Category = "Map")
	bool bOverrideClass = false;

	UPROPERTY(EditAnywhere, Category = "Map", meta = (EditCondition = "bOverrideClass"))
	EPuffersRoadClass RoadClass = EPuffersRoadClass::Street;

	UPROPERTY(EditAnywhere, Category = "Map")
	bool bBridge = false;

	UPROPERTY(EditAnywhere, Category = "Map")
	bool bTunnel = false;

	UPROPERTY(EditAnywhere, Category = "Map")
	bool bOneWay = false;

	// Leave the roads inside off the map (service lanes, hidden areas).
	UPROPERTY(EditAnywhere, Category = "Map")
	bool bExclude = false;

	bool ContainsWorldPoint(const FVector& P) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Map")
	TObjectPtr<UBoxComponent> Box;
};
