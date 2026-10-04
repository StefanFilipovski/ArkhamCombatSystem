#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemySpawner.generated.h"

class AEnemyCharacter;
class AArkhamCharacterBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyDefeated, int32, Defeated, int32, Total);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEncounterComplete);

// Runs one encounter: keeps a crowd of enemies alive around the arena until TotalEnemies have been beaten.
// Place it in the level; where enemies appear comes from the SpawnPoints picked in the level.
UCLASS()
class ARKHAMCOMBAT_API AEnemySpawner : public AActor
{
    GENERATED_BODY()

public:
    AEnemySpawner();

    // True once every enemy of the encounter has been beaten (or the spawner has nothing to spawn)
    UFUNCTION(BlueprintPure, Category = "Encounter")
    bool IsEncounterComplete() const { return bComplete; }

    UFUNCTION(BlueprintPure, Category = "Encounter")
    int32 GetDefeatedCount() const { return DefeatedCount; }

    UFUNCTION(BlueprintPure, Category = "Encounter")
    int32 GetTotalEnemies() const { return TotalEnemies; }

    UFUNCTION(BlueprintPure, Category = "Encounter")
    int32 GetEnemiesRemaining() const { return TotalEnemies - DefeatedCount; }

    // Fires on every defeated enemy
    UPROPERTY(BlueprintAssignable, Category = "Encounter")
    FOnEnemyDefeated OnEnemyDefeated;

    // Fires once, when the last enemy of the encounter goes down
    UPROPERTY(BlueprintAssignable, Category = "Encounter")
    FOnEncounterComplete OnEncounterComplete;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(EditAnywhere, Category = "Encounter")
    TSubclassOf<AEnemyCharacter> EnemyClass;

    // Where enemies come from. Empty = random navigable spots within FallbackSpawnRadius of the spawner.
    UPROPERTY(EditInstanceOnly, Category = "Encounter")
    TArray<TObjectPtr<AActor>> SpawnPoints;

    UPROPERTY(EditAnywhere, Category = "Encounter", meta = (ClampMin = "1"))
    int32 InitialEnemies = 3;

    // Crowd size never grows past this
    UPROPERTY(EditAnywhere, Category = "Encounter", meta = (ClampMin = "1"))
    int32 MaxAliveEnemies = 5;

    // Enemies to beat before the demo is over
    UPROPERTY(EditAnywhere, Category = "Encounter", meta = (ClampMin = "1"))
    int32 TotalEnemies = 15;

    // Every this many defeats, one more enemy joins the crowd (up to MaxAliveEnemies). 0 = crowd stays at InitialEnemies.
    UPROPERTY(EditAnywhere, Category = "Encounter", meta = (ClampMin = "0"))
    int32 DefeatsPerExtraEnemy = 3;

    // Random delay (min, max seconds) before a replacement arrives
    UPROPERTY(EditAnywhere, Category = "Encounter")
    FVector2D RespawnDelay = FVector2D(1.f, 2.5f);

    // Spawn points closer to the player than this are skipped, so nobody pops in next to you
    UPROPERTY(EditAnywhere, Category = "Encounter")
    float MinSpawnDistance = 700.f;

    // Prefer spawn points the camera isn't looking at
    UPROPERTY(EditAnywhere, Category = "Encounter")
    bool bPreferOffscreenSpawns = true;

    UPROPERTY(EditAnywhere, Category = "Encounter")
    float FallbackSpawnRadius = 1500.f;

    // Seconds after the last enemy falls before the level restarts. 0 = stay in the level.
    UPROPERTY(EditAnywhere, Category = "Encounter", meta = (ClampMin = "0"))
    float RestartDelayAfterComplete = 6.f;

private:
    int32 SpawnedCount = 0;
    int32 DefeatedCount = 0;
    int32 PendingSpawns = 0;
    bool bComplete = false;

    UPROPERTY()
    TSet<TObjectPtr<AEnemyCharacter>> LivingEnemies;

    TWeakObjectPtr<AActor> LastSpawnPoint;

    FTimerHandle SpawnTimer;
    FTimerHandle RestartTimer;

    int32 GetDesiredAliveCount() const;
    void SpawnInitialEnemies();
    void QueueSpawns();
    void SpawnQueuedEnemy();
    AEnemyCharacter* TrySpawnEnemy();
    bool PickSpawnLocation(FVector& OutLocation);
    bool PickSpawnPoint(FVector& OutLocation);
    bool PickNavigableLocation(FVector& OutLocation) const;
    bool IsSpotTaken(const FVector& Location) const;

    UFUNCTION()
    void HandleEnemyDied(AArkhamCharacterBase* Character);

    UFUNCTION()
    void HandleEnemyDestroyed(AActor* DestroyedActor);

    void RegisterDefeat(AEnemyCharacter* Enemy);
    void CompleteEncounter();
    void RestartLevel();
};
