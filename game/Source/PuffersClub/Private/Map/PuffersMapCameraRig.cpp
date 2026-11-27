#include "Map/PuffersMapCameraRig.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

APuffersMapCameraRig::APuffersMapCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetReplicates(false);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->bConstrainAspectRatio = false;
}

void APuffersMapCameraRig::Begin(APlayerController* InPC, const FVector& FromLocation, const FRotator& FromRotation)
{
	PC = InPC;
	const APawn* Pawn = InPC ? InPC->GetPawn() : nullptr;
	Focus = TargetFocus = Pawn ? Pawn->GetActorLocation() : FromLocation;
	// Start close to the player's own camera so the fly-out reads as one move.
	Distance = FMath::Max((double)MinDistanceCm, FVector::Distance(FromLocation, Focus) + 1500.0);
	TargetDistance = 60000.0;
	Camera->SetFieldOfView(FieldOfView);
	SetActorLocationAndRotation(FromLocation, FromRotation);
	if (InPC)
	{
		InPC->SetViewTargetWithBlend(this, 0.75f, VTBlend_EaseInOut, 2.f);
	}
}

void APuffersMapCameraRig::End()
{
	if (bEnding)
	{
		return;
	}
	bEnding = true;
	if (APlayerController* Controller = PC.Get())
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			Controller->SetViewTargetWithBlend(Pawn, 0.55f, VTBlend_EaseInOut, 2.f);
		}
	}
	SetLifeSpan(0.8f);
}

void APuffersMapCameraRig::SetTarget(const FVector& WorldFocus, double VisibleWidthCm)
{
	TargetFocus = WorldFocus;
	const double HalfFov = FMath::DegreesToRadians(FieldOfView * 0.5);
	TargetDistance = FMath::Clamp(VisibleWidthCm * 0.5 / FMath::Tan(HalfFov), (double)MinDistanceCm, (double)MaxDistanceCm);
}

void APuffersMapCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bEnding)
	{
		return;
	}
	const double K = 1.0 - FMath::Exp(-DeltaSeconds * 6.0);
	Focus = FMath::Lerp(Focus, TargetFocus, K);
	Distance = FMath::Exp(FMath::Lerp(FMath::Loge(Distance), FMath::Loge(TargetDistance), K));

	// North-up: map north is world -Y, so the camera looks along -Y from the south.
	const FRotator Rot(-Pitch, -90.f, 0.f);
	const FVector Location = Focus - Rot.Vector() * Distance;
	SetActorLocationAndRotation(Location, Rot);
}
