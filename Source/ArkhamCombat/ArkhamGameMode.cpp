#include "ArkhamGameMode.h"
#include "Characters/PlayerCharacter.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

AArkhamGameMode::AArkhamGameMode()
{
    

    EnemyManager = CreateDefaultSubobject<UEnemyManagerComponent>(
        TEXT("EnemyManager")
    );
}

void AArkhamGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (BackgroundMusic)
    {
        // Not persisted across level loads, so every restart begins the track again
        MusicComponent = UGameplayStatics::SpawnSound2D(this, BackgroundMusic, 1.f, 1.f, 0.f, nullptr, false, false);
        if (MusicComponent)
        {
            MusicComponent->FadeIn(MusicFadeInTime, MusicVolume);
        }
    }

    // The player is already spawned when the level starts
    if (AArkhamCharacterBase* Player = Cast<AArkhamCharacterBase>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        Player->OnDied.AddDynamic(this, &AArkhamGameMode::HandlePlayerDied);
    }
}

void AArkhamGameMode::HandlePlayerDied(AArkhamCharacterBase* Player)
{
    if (MusicComponent)
    {
        MusicComponent->FadeOut(MusicFadeOutTime, 0.f);
    }
}
