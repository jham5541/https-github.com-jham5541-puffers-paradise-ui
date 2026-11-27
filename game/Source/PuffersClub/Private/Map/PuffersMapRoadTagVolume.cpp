#include "Map/PuffersMapRoadTagVolume.h"

#include "Components/BoxComponent.h"

APuffersMapRoadTagVolume::APuffersMapRoadTagVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(2000.f, 600.f, 800.f));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->ShapeColor = FColor(255, 201, 74);
	Box->bHiddenInGame = true;
	RootComponent = Box;
	SetActorHiddenInGame(true);
	// Needed by the surveyor at runtime, but never by players.
	bNetLoadOnClient = false;
}

bool APuffersMapRoadTagVolume::ContainsWorldPoint(const FVector& P) const
{
	const FVector Local = Box->GetComponentTransform().InverseTransformPosition(P);
	const FVector E = Box->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= E.X && FMath::Abs(Local.Y) <= E.Y && FMath::Abs(Local.Z) <= E.Z;
}
