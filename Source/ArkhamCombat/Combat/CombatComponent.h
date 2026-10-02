#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/StrikeAnalysis.h"
#include "Combat/AttackData.h"
#include "CombatComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class AArkhamCharacterBase;
class AEnemyCharacter;
class UMotionWarpingComponent;
class URootMotionModifier;
class UCameraShakeBase;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFinalBlow, AActor*, Victim);

// One combo, in order - the last attack is its finisher
USTRUCT(BlueprintType)
struct FComboChain
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Combat")
    TArray<TObjectPtr<UAttackData>> Attacks;

    // A special combo: one press plays every attack in the chain, each flowing into the next
    UPROPERTY(EditAnywhere, Category = "Combat")
    bool bPlayOnSinglePress = false;
};

UENUM(BlueprintType)
enum class ECombatState : uint8
{
    Idle        UMETA(DisplayName = "Idle"),
    Attacking   UMETA(DisplayName = "Attacking"),
    Countering  UMETA(DisplayName = "Countering"),
    Staggered   UMETA(DisplayName = "Staggered")
};

// Phases of a single attack.
// Windup   - travelling/turning toward the target, input is buffered
// Active   - hit window open (between EnableHitbox and DisableHitbox)
// Recovery - hit window closed, attack input chains immediately, movement cancels
UENUM(BlueprintType)
enum class EAttackPhase : uint8
{
    None,
    Windup,
    Active,
    Recovery
};

/**
 * Freeflow-style melee combat.
 *
 * Flow per attack press:
 *   1. Pick a target from the camera-relative stick direction (falls back to current target / facing)
 *   2. Pick an animation by distance: close -> next combo section, far -> transition kick
 *   3. Travel so the player arrives at strike distance exactly when the hit window opens
 *      (contact time is read from the EnableHitbox notify in the montage section)
 *   4. During the hit window, sweep the attacking socket; if the committed target was
 *      somehow missed, it is still hit when the window closes (freeflow never whiffs)
 *   5. Presses during windup/active are buffered and fire when the window closes
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class ARKHAMCOMBAT_API UCombatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCombatComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // --- Input ---
    void TryAttack();

    // Counters the enemy whose punch lands soonest, if one is winding up in range
    void TryCounter();

    // World-space, camera-relative movement input. Length = stick magnitude.
    void SetMovementInput(const FVector& WorldInput);

    // True while combat is driving the character and normal movement should be skipped
    bool IsMovementLocked() const { return CombatState != ECombatState::Idle; }

    // --- Taking Hits ---
    // Called by the owner after damage has been applied
    void ReceiveHit(AActor* Attacker, const FCombatHit& Hit);

    // Counters are untouchable
    bool IsInvulnerable() const { return CombatState == ECombatState::Countering; }

    // True for the enemy the player is currently swinging at, countering, or chaining a combo on
    bool IsEngaging(const AEnemyCharacter* Enemy) const;

    // --- Anim Notify Callbacks ---
    // MontageInstanceID lets us ignore notifies from montages that are blending out
    void EnableHitbox(const UAnimSequenceBase* Animation, int32 MontageInstanceID);
    void DisableHitbox(const UAnimSequenceBase* Animation, int32 MontageInstanceID);

    // From the Counter Impact notify in the counter attack's montage
    void OnCounterImpact(const UAnimSequenceBase* Animation);

    // --- Getters ---
    ECombatState GetCombatState() const { return CombatState; }
    int32 GetComboCount() const { return ComboCount; }
    bool IsHitboxActive() const { return AttackPhase == EAttackPhase::Active; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    EAttackPhase GetAttackPhase() const { return AttackPhase; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    AEnemyCharacter* GetCurrentTarget() const { return CurrentTarget.Get(); }

    // Fired when a hit knocks out the last enemy standing
    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FOnFinalBlow OnFinalBlow;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY()
    TObjectPtr<AArkhamCharacterBase> OwnerCharacter;

    UPROPERTY()
    TObjectPtr<UMotionWarpingComponent> MotionWarping;

    // --- State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    ECombatState CombatState = ECombatState::Idle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    EAttackPhase AttackPhase = EAttackPhase::None;

    // Hits landed in the current chain
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    int32 ComboCount = 0;

    // --- Combo ---
    // Time after an attack finishes before the chain and combo counter reset
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float ComboWindowDuration = 0.8f;

    // --- Targeting ---
    // Max distance at which an enemy can be selected as a target
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting")
    float LungeRange = 500.f;

    // Targets further than this get a transition kick instead of a combo attack
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting")
    float CloseAttackRange = 250.f;

    // Minimum dot between stick direction and enemy direction to be considered
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting")
    float DirectionalTargetMinDot = 0.2f;

    // --- Movement ---
    // Used when a montage section has no EnableHitbox notify to time the approach
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float DefaultContactTime = 0.2f;

    // Caps how fast the approach can move so long lunges don't look like teleports
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float MaxApproachSpeed = 2500.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float TurnSpeed = 18.f;

    // Recovery must last this long before stick input cancels it, so mashing through a chain doesn't drop it
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float MovementCancelDelay = 0.15f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    bool bAllowMovementCancel = true;

    // Attacks whose animation travels at least MinWarpRootTravel before contact keep their root motion,
    // warped by the MotionWarping component to land on the target. Others use the code-driven approach.
    // Warp windows are added from code - no montage setup needed.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    bool bUseMotionWarping = true;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float MinWarpRootTravel = 50.f;

    // Root motion scale after contact on warped attacks, so they don't keep travelling into the target
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    float PostContactRootMotionScale = 0.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Movement")
    FName WarpTargetName = TEXT("AttackTarget");

    // --- Strike Analysis ---
    // Read the contact frame (striking limb at full extension) and which limb strikes from the animation itself,
    // instead of trusting EnableHitbox notify placement
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    bool bAutoDetectStrike = true;

    // Candidate striking bones
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    TArray<FName> StrikeBones;

    // Contact is when the limb gets this far from its wind-up to full extension (1 = the peak itself)
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike", meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float ContactExtension = 0.85f;

    // Derive strike distance from the limb's reach so it lands just inside the target's capsule
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    bool bAutoStrikeDistance = true;

    // How far past the target's capsule surface the limb reaches at contact
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    float StrikePenetration = 25.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    float MinStrikeDistance = 60.f;

    // With auto-detected contact, the hit window opens this long before contact
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    float HitWindowLead = 0.05f;

    // Single-press combos start their next hit this many seconds before the current section ends
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Strike")
    float AutoContinueLead = 0.03f;

    // --- Hit Detection ---
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    float HitboxRadius = 15.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    float FootHitboxRadius = 20.f;

    // Hit window length after contact when it isn't driven by notifies
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    float DefaultActiveDuration = 0.15f;

    // If the sweep misses the committed target but it's within strike distance + this, hit it anyway
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    bool bGuaranteeTargetHit = true;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    float TargetHitTolerance = 60.f;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit")
    bool bDrawHitDebug = false;

    // --- Impact ---
    // Played on every hit the player lands; how hard it shakes and freezes comes from the attack's data
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    TSubclassOf<UCameraShakeBase> HitCameraShake;

    // Spawned at the point of contact
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    TObjectPtr<UNiagaraSystem> HitEffect;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    TObjectPtr<USoundBase> HitSound;

    // Slow motion when the last enemy goes down
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    bool bSlowMoOnFinalBlow = true;

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    float FinalBlowTimeDilation = 0.2f;

    // Real-time seconds
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Impact")
    float FinalBlowDuration = 1.2f;

    // --- Attacks ---
    // The combo, in order. The last one is the finisher. Add, remove or reorder to change the combo.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attacks")
    TArray<TObjectPtr<UAttackData>> ComboAttacks;

    // More combos. Every new combo picks one at random from Combo Attacks and these.
    // Sections cut from one multi-hit animation make a chain that flows like the original.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attacks")
    TArray<FComboChain> ExtraComboChains;

    // Picked at random to switch targets, lunge to far targets, and as the killing-blow finisher
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attacks")
    TArray<TObjectPtr<UAttackData>> FlashyAttacks;

    // Switching to a different enemy mid-chain opens with a flashy attack
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attacks")
    bool bFlashyKickOnTargetSwitch = true;

    // The combo's last hit becomes a flashy attack when it will kill the target
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Attacks")
    bool bFlashyKickOnKillingFinisher = true;

    // --- Counter ---
    // Player half ("Vic") of the paired counter animation plus how hard it hits. The enemy plays the other half.
    // Its montage needs a Counter Impact notify on the frame the strike connects - that's when the damage lands.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Counter")
    TObjectPtr<UAttackData> CounterAttack;

    // How close a winding-up enemy must be to counter it
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Counter")
    float CounterRange = 450.f;

    // Distance the two characters are lined up at for the paired animation
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Counter")
    float CounterPairDistance = 95.f;

    // After the impact, attack/counter/move input can cut the rest of the counter animation
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Counter")
    float CounterCancelDelay = 0.25f;

    // --- Hit Reactions ---
    // Picked at random when the player is hit
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    TArray<TObjectPtr<UAnimMontage>> HitReactMontages;

    // Played instead when the hit comes from behind, without turning around first.
    // Empty = turn to face the attacker and use Hit React Montages.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    TArray<TObjectPtr<UAnimMontage>> HitReactMontagesFromBehind;

    // Turn off Enable Auto Blend Out on this montage so the player stays down
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    TObjectPtr<UAnimMontage> DeathMontage;

    // Killing blow from behind. Empty = turn around and use Death Montage. Auto Blend Out off here too.
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    TObjectPtr<UAnimMontage> DeathMontageFromBehind;

    // Attackers more than this many degrees away from where the player faces count as behind
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions", meta = (ClampMin = "90", ClampMax = "180"))
    float FromBehindAngle = 120.f;

    // How long a hit locks out input; the reaction animation can play on past this
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    float HitStunDuration = 0.6f;

    // Played when the player is hit; how hard it shakes and freezes comes from the enemy attack's data
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Hit Reactions")
    TSubclassOf<UCameraShakeBase> PlayerHitCameraShake;

private:
    struct FActiveAttack
    {
        const UAttackData* Data = nullptr;
        TWeakObjectPtr<AEnemyCharacter> Target;
        const UAnimMontage* Montage = nullptr;
        FName Section;
        FName Socket;
        int32 MontageInstanceID = INDEX_NONE;
        float SectionStart = 0.f;
        float SectionEnd = 0.f;
        float StrikeDistance = 0.f;
        float ContactTime = 0.f;
        float Elapsed = 0.f;
        float RecoveryElapsed = 0.f;
        bool bHasHitNotifies = false;
        bool bUseWarp = false;
        bool bContactReached = false;
        bool bIsFinisher = false;
        bool bIsTransition = false;
        bool bIsFlashy = false;
        // Part of a single-press combo with more attacks after it - the next one starts on its own
        bool bAutoContinue = false;
        bool bTargetHit = false;
        bool bHasLastSocketLocation = false;
        FVector LastSocketLocation = FVector::ZeroVector;
        TArray<TWeakObjectPtr<AActor>> HitActors;
    };

    // --- Attack Flow ---
    void StartAttack();
    const UAttackData* PickFlashyAttack();
    void OpenHitWindow();
    void CloseHitWindow();
    void FinishAttack();
    void CancelAttack();
    void ResetCombo();
    void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 MontageInstanceID);
    bool IsCurrentAttackNotify(const UAnimSequenceBase* Animation, int32 MontageInstanceID) const;

    // --- Strike Analysis ---
    FStrikeInfo AnalyzeStrike(const UAnimMontage* Montage, FName Section);
    TMap<TPair<const UAnimMontage*, FName>, FStrikeInfo> StrikeCache;

    // --- Movement ---
    void UpdateFacing(float DeltaTime);
    void UpdateApproach(float DeltaTime);
    void UpdateWarpTarget();
    void AddWarpModifiers();
    void ClearWarpModifiers();
    void LockMovement();
    void UnlockMovement();

    TArray<TWeakObjectPtr<URootMotionModifier>> WarpModifiers;

    // --- Hit Detection ---
    void UpdateHitDetection();
    void OnContact();
    void ApplyGuaranteedHit(AEnemyCharacter* Target);
    void ApplyHit(AArkhamCharacterBase* Victim, const FVector& ImpactPoint);
    bool IsTargetInReach(const AEnemyCharacter* Target) const;
    float GetHitRadius() const;

    // --- Counter ---
    void StartCounter(AEnemyCharacter* Enemy);
    void UpdateCounter(float DeltaTime);
    void EndCounter(bool bStopAnimation);
    bool CanLeaveCounter() const;
    void ApplyCounterImpact();

    TWeakObjectPtr<AEnemyCharacter> CounterTarget;
    float CounterElapsed = 0.f;
    float CounterLength = 0.f;
    bool bCounterImpactDone = false;
    float CounterImpactElapsed = 0.f;

    // --- Hit Reactions ---
    void CancelActiveAttack();
    void EndHitStun();
    void PlayReaction(UAnimMontage* Montage);

    // Counter / hit react currently playing, so input can cut it short
    TWeakObjectPtr<UAnimMontage> ReactionMontage;
    FTimerHandle HitStunTimer;

    // --- Impact ---
    void PlayHitImpact(const FVector& ImpactPoint, float ShakeScale);
    void TriggerHitPause(AActor* HitActor, float Duration);
    void EndHitPause();
    bool IsLastEnemyStanding(const AActor* Victim) const;
    void StartFinalBlow(AActor* Victim);
    void EndFinalBlow();

    // --- Targeting ---
    AEnemyCharacter* SelectTarget() const;
    AEnemyCharacter* FindTargetInDirection(const FVector& Direction, float MinDot, bool bAllowStaggered = false) const;
    bool IsTargetable(const AEnemyCharacter* Enemy, bool bAllowStaggered = false) const;
    bool HasRecentMovementInput() const;
    void UpdateTargetHighlight();

    FActiveAttack ActiveAttack;
    TWeakObjectPtr<AEnemyCharacter> CurrentTarget;
    TWeakObjectPtr<AEnemyCharacter> HighlightedTarget;

    int32 ChainIndex = 0;
    // 0 = ComboAttacks, n = ExtraComboChains[n - 1]
    int32 ActiveChain = 0;
    bool bAttackBuffered = false;

    void PickComboChain();
    const TArray<TObjectPtr<UAttackData>>& GetActiveChain() const;
    bool IsActiveChainSinglePress() const;

    // Who the chain was last hitting, to detect target switches. Cleared when the combo resets.
    TWeakObjectPtr<AEnemyCharacter> LastAttackTarget;
    bool bChainHasTarget = false;
    int32 LastFlashyKickIndex = INDEX_NONE;

    FVector MovementInput = FVector::ZeroVector;
    double LastMovementInputTime = -1.0;

    FTimerHandle ComboResetTimer;
    FTimerHandle HitPauseTimer;
    TArray<TWeakObjectPtr<AActor>> HitPausedActors;

    FTimerHandle FinalBlowTimer;
    bool bFinalBlowActive = false;
};
