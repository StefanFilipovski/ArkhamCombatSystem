#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyManagerComponent.generated.h"

class AEnemyCharacter;
class APlayerCharacter;

UENUM(BlueprintType)
enum class EEnemyGroupBehavior : uint8
{
    Solo,       // 1 enemy - aggressive, stays close
    Pair,       // 2 enemies - flank left and right
    Group,      // 3-4 enemies - surround properly  
    Mob         // 5+ enemies - outer ring forms
};

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class ARKHAMCOMBAT_API UEnemyManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEnemyManagerComponent();

protected:
    virtual void BeginPlay() override;

    // How far enemies stand from player
    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float SurroundRadius = 200.f;

    // How often to update positions
    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float UpdateInterval = 0.5f;

    // Max enemies that can attack at once
    UPROPERTY(EditDefaultsOnly, Category = "AI")
    int32 MaxSimultaneousAttackers = 2;

    // Enemies closer than this (beyond their own wander radius) to their slot are left to wander on their own
    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float SlotReturnSlack = 100.f;

    // --- Attacks ---
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    bool bEnemiesAttack = true;

    // Only enemies this close to the player will start an attack
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    float AttackEngageRange = 500.f;

    // Random gap (min, max seconds) between attacks when fighting a single enemy
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    FVector2D SoloAttackInterval = FVector2D(3.5f, 5.5f);

    // Gap once the fight has CrowdSize or more enemies - blends between the two
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    FVector2D CrowdAttackInterval = FVector2D(1.5f, 2.5f);

    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    int32 CrowdSize = 6;

    // One simultaneous attacker per this many enemies in the fight, capped by MaxSimultaneousAttackers
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    int32 EnemiesPerAttacker = 3;

    // Grace period at the start of a fight
    UPROPERTY(EditDefaultsOnly, Category = "AI|Attacks")
    float InitialAttackDelay = 2.f;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY()
    TObjectPtr<APlayerCharacter> Player;

    UPROPERTY()
    TArray<TObjectPtr<AEnemyCharacter>> ActiveEnemies;

    float TimeSinceLastUpdate = 0.f;
    float TimeUntilNextAttack = 0.f;
    bool bAttackTimerStarted = false;

    void UpdateAttacks(float DeltaTime);
    void UpdateEnemyPositions();
    void FindAllEnemies();
    TArray<FVector> CalculateSurroundPositions(int32 EnemyCount);
    FVector CalculateSeparationForce(AEnemyCharacter* Enemy);
    EEnemyGroupBehavior GetGroupBehavior() const;

};