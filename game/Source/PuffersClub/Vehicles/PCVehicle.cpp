#include "Vehicles/PCVehicle.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"

APCVehicle::APCVehicle()
{
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 650.f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
}

void APCVehicle::PawnClientRestart()
{
	Super::PawnClientRestart();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DrivingMappingContext)
			{
				Subsystem->AddMappingContext(DrivingMappingContext, 1);
			}
		}
	}
}

void APCVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (ThrottleAction)
		{
			Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &APCVehicle::Throttle);
			Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &APCVehicle::Throttle);
		}
		if (SteerAction)
		{
			Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &APCVehicle::Steer);
			Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &APCVehicle::Steer);
		}
		if (BrakeAction)
		{
			Input->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &APCVehicle::Brake);
			Input->BindAction(BrakeAction, ETriggerEvent::Completed, this, &APCVehicle::Brake);
		}
		if (HandbrakeAction)
		{
			Input->BindAction(HandbrakeAction, ETriggerEvent::Started, this, &APCVehicle::HandbrakePressed);
			Input->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &APCVehicle::HandbrakeReleased);
		}
	}
}

void APCVehicle::Throttle(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetThrottleInput(Value.Get<float>());
}

void APCVehicle::Steer(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetSteeringInput(Value.Get<float>());
}

void APCVehicle::Brake(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetBrakeInput(Value.Get<float>());
}

void APCVehicle::HandbrakePressed(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetHandbrakeInput(true);
}

void APCVehicle::HandbrakeReleased(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetHandbrakeInput(false);
}
