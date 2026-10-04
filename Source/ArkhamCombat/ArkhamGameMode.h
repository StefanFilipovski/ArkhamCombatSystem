#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AI/EnemyManagerComponent.h"
#include "ArkhamGameMode.generated.h"

class USoundBase;
class UAudioComponent;
class AArkhamCharacterBase;

UCLASS()
class ARKHAMCOMBAT_API AArkhamGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AArkhamGameMode();

    UPROPERTY(VisibleAnywhere, Category = "AI")
    UEnemyManagerComponent* EnemyManager;

protected:
    virtual void BeginPlay() override;

    // Background track for the whole level
    UPROPERTY(EditDefaultsOnly, Category = "Audio")
    TObjectPtr<USoundBase> BackgroundMusic;

    UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0"))
    float MusicVolume = 0.4f;

    UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0"))
    float MusicFadeInTime = 2.f;

    // Fades out when the player dies, ahead of the level restart
    UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0"))
    float MusicFadeOutTime = 3.f;

private:
    UFUNCTION()
    void HandlePlayerDied(AArkhamCharacterBase* Player);

    UPROPERTY()
    TObjectPtr<UAudioComponent> MusicComponent;
};
