#include "Core/PCGameMode.h"
#include "Characters/PCCharacter.h"
#include "PuffersClub.h"

APCGameMode::APCGameMode()
{
	DefaultPawnClass = APCCharacter::StaticClass();
}

void APCGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogPuffersClub, Log, TEXT("Player joined: %s"), *GetNameSafe(NewPlayer));
	// Phase 4: authenticate against backend, load persistent character state.
}

void APCGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogPuffersClub, Log, TEXT("Player left: %s"), *GetNameSafe(Exiting));
	// Phase 4: flush persistent state and write transaction log entries.
	Super::Logout(Exiting);
}
