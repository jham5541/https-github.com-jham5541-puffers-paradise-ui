#include "Weapons/PCWeaponComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/PCAttributeSet.h"
#include "GameFramework/Pawn.h"
#include "Heat/PCHeatComponent.h"
#include "PuffersClub.h"

void UPCWeaponComponent::RequestFire(const FVector& Origin, const FVector& Direction)
{
	ServerFire(Origin, Direction.GetSafeNormal());
}

bool UPCWeaponComponent::ServerFire_Validate(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	// Reject impossible muzzle origins outright; rate limiting is handled
	// gracefully in _Implementation so lag spikes don't disconnect players.
	const AActor* Owner = GetOwner();
	return Owner != nullptr && FVector::Dist(Origin, Owner->GetActorLocation()) <= MaxOriginSlack;
}

void UPCWeaponComponent::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (LastFireTime >= 0.0 && (Now - LastFireTime) < FireInterval)
	{
		return; // firing faster than the weapon allows — drop the request
	}
	LastFireTime = Now;

	const FVector End = Origin + FVector(Direction) * Range;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponFire), /*bTraceComplex*/ true, Owner);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Pawn, Params);
	const FVector ImpactPoint = bHit ? Hit.ImpactPoint : End;

	if (bHit)
	{
		if (const IAbilitySystemInterface* TargetASI = Cast<IAbilitySystemInterface>(Hit.GetActor()))
		{
			if (UAbilitySystemComponent* TargetASC = TargetASI->GetAbilitySystemComponent())
			{
				TargetASC->ApplyModToAttribute(
					UPCAttributeSet::GetHealthAttribute(), EGameplayModOp::Additive, -Damage);
				UE_LOG(LogPuffersClub, Verbose, TEXT("%s hit %s for %.0f"),
					*GetNameSafe(Owner), *GetNameSafe(Hit.GetActor()), Damage);
			}
		}
	}

	// Firing a weapon is a crime signal; witnesses/cameras scale it later.
	if (UPCHeatComponent* Heat = Owner->FindComponentByClass<UPCHeatComponent>())
	{
		Heat->ReportCrime(HeatPerShot);
	}

	MulticastFireFX(Origin, ImpactPoint);
}

void UPCWeaponComponent::MulticastFireFX_Implementation(FVector_NetQuantize Origin, FVector_NetQuantize ImpactPoint)
{
	// Cosmetic only: muzzle flash, tracer, impact effects hang off this event.
	OnWeaponFired.Broadcast(Origin, ImpactPoint);
}
