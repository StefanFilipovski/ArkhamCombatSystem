#include "ArkhamGameMode.h"
#include "Characters/PlayerCharacter.h"

AArkhamGameMode::AArkhamGameMode()
{
    // We'll assign the BP version of PlayerCharacter as default pawn
    // This gets set in the Blueprint child we create next

    EnemyManager = CreateDefaultSubobject<UEnemyManagerComponent>(
        TEXT("EnemyManager")
    );
}