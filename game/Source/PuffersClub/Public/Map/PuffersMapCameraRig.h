// 3D map view: a camera that flies up from the player and looks down at the real (photoreal) world.
// The map view drives it with a focus point and a visible width; World Partition streams around it because it
// becomes the player's view target, and HLODs fill the distance.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PuffersMapCameraRig.generated.h"

class UCameraComponent;

UCLASS(NotPlaceable, Transient)
class PUFFERSCLUB_API APuffersMapCameraRig : public AActor
{
	GENERATED_BODY()

public:
	APuffersMapCameraRig();

	virtual void Tick(float DeltaSeconds) override;

	void Begin(APlayerController* InPC, const FVector& FromLocation, const FRotator& FromRotation);
	// Blends back to the pawn and destroys itself after the blend.
	void End();

	// Focus point (world cm) and the width of ground (cm) that should fill the screen.
	void SetTarget(const FVector& WorldFocus, double VisibleWidthCm);
	FVector GetFocus() const { return Focus; }

	// Degrees below the horizon (90 = straight down).
	UPROPERTY(EditAnywhere, Category = "Map")
	float Pitch = 58.f;

	UPROPERTY(EditAnywhere, Category = "Map")
	float FieldOfView = 55.f;

	UPROPERTY(EditAnywhere, Category = "Map")
	float MinDistanceCm = 6000.f;

	UPROPERTY(EditAnywhere, Category = "Map")
	float MaxDistanceCm = 1200000.f;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	TWeakObjectPtr<APlayerController> PC;
	FVector Focus = FVector::ZeroVector;
	FVector TargetFocus = FVector::ZeroVector;
	double Distance = 20000.0;
	double TargetDistance = 20000.0;
	bool bEnding = false;
};
