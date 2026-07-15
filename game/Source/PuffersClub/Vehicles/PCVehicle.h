#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "PCVehicle.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Base drivable vehicle (Chaos Vehicles). Concrete vehicles are Blueprint
 * subclasses that assign the skeletal mesh, wheel setup, and input assets.
 * All vehicle designs are original / fictional manufacturers.
 */
UCLASS()
class PUFFERSCLUB_API APCVehicle : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	APCVehicle();

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	void Throttle(const FInputActionValue& Value);
	void Steer(const FInputActionValue& Value);
	void Brake(const FInputActionValue& Value);
	void HandbrakePressed(const FInputActionValue& Value);
	void HandbrakeReleased(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DrivingMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> BrakeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> HandbrakeAction;
};
