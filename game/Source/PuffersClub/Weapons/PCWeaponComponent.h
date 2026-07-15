#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCWeaponComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponFired, FVector, Origin, FVector, ImpactPoint);

/**
 * Server-validated hitscan weapon. The client requests a shot; the server
 * enforces fire rate and origin sanity, runs the authoritative trace, applies
 * damage through the ability system, and reports the crime to the heat system.
 * The client never decides damage.
 */
UCLASS(ClassGroup = (PuffersClub), meta = (BlueprintSpawnableComponent))
class PUFFERSCLUB_API UPCWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Called locally by input; forwards the request to the server. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void RequestFire(const FVector& Origin, const FVector& Direction);

	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnWeaponFired OnWeaponFired;

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireFX(FVector_NetQuantize Origin, FVector_NetQuantize ImpactPoint);

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Damage = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Range = 10000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon", meta = (ClampMin = "0.05"))
	float FireInterval = 0.15f;

	/** How far the claimed muzzle origin may be from the owning pawn. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float MaxOriginSlack = 300.f;

	/** Base heat added per shot fired in public (scaled by witnesses later). */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float HeatPerShot = 5.f;

	double LastFireTime = -1.0;
};
