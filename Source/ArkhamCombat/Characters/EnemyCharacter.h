#pragma once

#include "CoreMinimal.h"
#include "Characters/ArkhamCharacterBase.h"
#include "Combat/StrikeAnalysis.h"
#include "EnemyCharacter.generated.h"

class APlayerCharacter;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UAnimSequenceBase;

UENUM(BlueprintType)
enum class EEnemyAttackState : uint8
{
    None,
    Telegraph,      // indicator up, closing in before the swing starts
    Striking,       // swing playing, up to the contact frame
    Recovering,     // after contact
    Countered       // playing the paired counter reaction
};

UCLASS()
class ARKHAMCOMBAT_API AEnemyCharacter : public AArkhamCharacterBase
{
    GENERATED_BODY()

public:
    AEnemyCharacter();

protected:
    virtual void BeginPlay() override;

    // --- Animations ---
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Animations")
    UAnimMontage* HitReactMontage;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Animations")
    UAnimMontage* StaggerMontage_Large;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Animations")
    UAnimMontage* DeathMontage;

    // --- Outline ---
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    UMaterialInterface* OutlineMaterial;

    // --- Telegraph ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bIsTelegraphing = false;

    // --- AI ---
    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float RotationSpeed = 5.f;

    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float WanderRadius = 100.f;

    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float MinWanderInterval = 1.5f;

    UPROPERTY(EditDefaultsOnly, Category = "AI")
    float MaxWanderInterval = 3.5f;

    // --- Knockback ---
    // Light hit reactions are in place, so the push back comes from code (staggers use root motion).
    // Scaled per hit by the attack's KnockbackScale.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Knockback")
    float HitKnockbackDistance = 60.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Knockback")
    float HitKnockbackDuration = 0.2f;

    // Section names picked at random from HitReactMontage for a normal hit
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Animations")
    TArray<FName> HitReactSections;

    // Section names picked at random from StaggerMontage_Large for a knockdown
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Animations")
    TArray<FName> StaggerSections;

    // --- Attack ---
    // Picked at random per attack. Each defines its animation and how hard it hits;
    // contact frame and punching hand are read from the animation.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    TArray<TObjectPtr<UAttackData>> Attacks;

    // Paired reaction when the player counters (the "Att" half of the paired animation)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    TObjectPtr<UAnimMontage> CounteredMontage;

    // Time from the indicator appearing to the punch landing - the player's counter window
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float CounterWindow = 0.8f;

    // Swings are slowed toward this so they fill the counter window; anything left is spent closing in
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float MinAttackPlayRate = 0.5f;

    // How far past the player's capsule surface the fist lands
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float AttackStrikePenetration = 25.f;

    // Extra distance at contact that still counts as a hit (moving away is how you dodge)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float AttackReachTolerance = 50.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float AttackMaxApproachSpeed = 900.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float AttackTurnSpeed = 12.f;

    // The punch lands when the fist gets this far from its wind-up to full extension (1 = the peak itself)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float ContactExtension = 0.85f;

    // Can't start an attack for this long after being hit - enemies you're beating on don't swing back
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attack")
    float AttackLockoutAfterHit = 1.5f;

    // --- Indicator ---
    // Camera-facing quad around the head (or the striking hand). Its mesh and material are set on the
    // component in the Blueprint. Code only places it and feeds the material how close the punch is;
    // the material decides how that looks.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Indicator")
    TObjectPtr<UStaticMeshComponent> AttackIndicatorRing;

    // Scalar material parameter set from 0 (ring appears) to 1 (punch lands)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    FName IndicatorUrgencyParameter = TEXT("Urgency");

    // Put the ring on the hand that's about to punch instead of around the head
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    bool bIndicatorOnStrikingHand = false;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    FName IndicatorHeadBone = TEXT("head");

    // World-space offset from the bone
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    FVector IndicatorOffset = FVector(0.f, 0.f, 8.f);

    // Quad size in cm (the ring is ~70% of it)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    float IndicatorHeadSize = 60.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Indicator")
    float IndicatorHandSize = 35.f;

public:
    virtual void Tick(float DeltaTime) override;

    virtual void TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator) override;
    virtual void Die() override;

    void ShowOutline();
    void HideOutline();
    void OnDeathAnimationEnd();
    void SetAssignedPosition(FVector NewPosition);

    // Enemies only exist to fight - always in a guard stance
    virtual bool IsInCombat() const override { return !bIsDead; }

    // --- Getters ---
    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsTelegraphing() const { return bIsTelegraphing; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsStaggered() const { return bIsStaggered; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsBeingHit() const { return bIsBeingHit; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsRecovering() const { return bIsRecovering; }

    float GetWanderRadius() const { return WanderRadius; }

    // --- Attacking ---
    bool CanStartAttack() const;
    void StartAttack(AActor* Target);

    // True from the indicator appearing until the punch lands
    bool IsCounterable() const;
    float GetTimeToContact() const;

    // Busy attacking or being countered - the enemy manager leaves these alone
    bool IsAttacking() const { return AttackState != EEnemyAttackState::None; }

    // Starts the paired reaction; the counterer applies the damage at its impact frame
    void PlayCountered(AActor* Counterer);
    void TakeCounterHit(const FCombatHit& Hit, AActor* DamageInstigator);

    UFUNCTION(BlueprintPure, Category = "Combat")
    EEnemyAttackState GetAttackState() const { return AttackState; }

private:
    // --- Attack State ---
    EEnemyAttackState AttackState = EEnemyAttackState::None;
    TWeakObjectPtr<AActor> AttackTarget;
    const UAttackData* CurrentAttack = nullptr;
    float AttackElapsed = 0.f;
    float TelegraphDelay = 0.f;     // closing in before the swing starts
    float ContactDelay = 0.f;       // swing start to contact at the chosen play rate
    float AttackEndTime = 0.f;
    float AttackPlayRate = 1.f;
    float AttackStrikeDistance = 100.f;
    FName AttackStrikeBone;
    double LastHitTime = -1000.0;
    TMap<const UAttackData*, FStrikeInfo> AttackStrikeCache;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> IndicatorMID;

    void UpdateAttack(float DeltaTime);
    void BeginStrike();
    void ResolveAttackContact();
    void EndAttack();
    void CancelAttack();
    void UpdateIndicator();
    void SetIndicatorVisible(bool bVisible);
    FStrikeInfo GetStrikeInfo(const UAttackData* Attack);
    void RestoreMovement();

    // --- State ---
    bool bIsStaggered = false;
    bool bIsBeingHit = false;
    bool bIsRecovering = false;

    // --- Timers ---
    FTimerHandle DeathRemovalTimer;
    FTimerHandle HitReactRecoveryTimer;

    // --- AI ---
    TWeakObjectPtr<APlayerCharacter> CachedPlayer;
    FTimerHandle WanderTimer;
    FVector AssignedPosition = FVector::ZeroVector;
    bool bHasAssignedPosition = false;

    // --- Knockback ---
    FVector KnockbackDirection = FVector::ZeroVector;
    float KnockbackDistance = 0.f;
    float KnockbackElapsed = 0.f;
    bool bKnockbackActive = false;

    // --- Internal ---
    void UpdateKnockback(float DeltaTime);
    void RemoveBody();
    void FacePlayer(float DeltaTime);
    void StartWandering();
    void DoWander();
    void OnHitReactEnd();
};