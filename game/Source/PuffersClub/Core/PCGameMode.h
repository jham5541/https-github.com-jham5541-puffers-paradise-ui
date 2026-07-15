#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PCGameMode.generated.h"

/**
 * Base game mode for the open world. Server-only object; owns spawn rules
 * and (in later phases) session hand-off to backend services.
 */
UCLASS()
class PUFFERSCLUB_API APCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APCGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
};
