#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AI/EnemyManagerComponent.h"
#include "ArkhamGameMode.generated.h"

UCLASS()
class ARKHAMCOMBAT_API AArkhamGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AArkhamGameMode();

    UPROPERTY(VisibleAnywhere, Category = "AI")
    UEnemyManagerComponent* EnemyManager;
};