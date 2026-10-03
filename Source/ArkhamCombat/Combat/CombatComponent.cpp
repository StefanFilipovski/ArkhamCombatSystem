#include "Combat/CombatComponent.h"
#include "Combat/AnimNotify_EnableHitbox.h"
#include "Combat/AnimNotify_CounterImpact.h"
#include "Combat/AnimNotifyState_HitWindow.h"
#include "Characters/ArkhamCharacterBase.h"
#include "Characters/EnemyCharacter.h"
#include "AI/EnemySpawner.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.h"
#include "Combat/StrikeAnalysis.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Camera/CameraShakeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
    // Time from section start to the first hit window notify, in montage seconds
    bool FindContactTime(const UAnimMontage* Montage, FName Section, float& OutContactTime)
    {
        float SectionStart = 0.f;
        float SectionEnd = 0.f;
        if (!StrikeAnalysis::GetSectionRange(Montage, Section, SectionStart, SectionEnd)) return false;

        bool bFound = false;
        OutContactTime = TNumericLimits<float>::Max();

        for (const FAnimNotifyEvent& Event : Montage->Notifies)
        {
            const bool bIsHitNotify =
                (Event.Notify && Event.Notify->IsA<UAnimNotify_EnableHitbox>()) ||
                (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_HitWindow>());
            if (!bIsHitNotify) continue;

            const float TriggerTime = Event.GetTriggerTime();
            if (TriggerTime < SectionStart || TriggerTime >= SectionEnd) continue;

            OutContactTime = FMath::Min(OutContactTime, TriggerTime - SectionStart);
            bFound = true;
        }

        return bFound;
    }
}

UCombatComponent::UCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    StrikeBones = { TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l") };
}

void UCombatComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<AArkhamCharacterBase>(GetOwner());
    if (!OwnerCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("CombatComponent owner is not an ArkhamCharacterBase!"));
        return;
    }

    // Analyze every attack up front so the first swing doesn't hitch
    if (bAutoDetectStrike)
    {
        TArray<const TArray<TObjectPtr<UAttackData>>*> Lists = { &ComboAttacks, &FlashyAttacks };
        for (const FComboChain& Chain : ExtraComboChains)
        {
            Lists.Add(&Chain.Attacks);
        }

        for (const TArray<TObjectPtr<UAttackData>>* List : Lists)
        {
            for (const UAttackData* Attack : *List)
            {
                if (Attack && Attack->Montage)
                {
                    AnalyzeStrike(Attack->Montage, Attack->Section);
                }
            }
        }
    }

    // Tell whoever sets up the montage if the impact frame hasn't been marked
    const UAnimMontage* CounterMontage = CounterAttack ? CounterAttack->Montage.Get() : nullptr;
    if (CounterMontage && !CounterMontage->Notifies.ContainsByPredicate([](const FAnimNotifyEvent& Event)
        {
            return Event.Notify && Event.Notify->IsA<UAnimNotify_CounterImpact>();
        }))
    {
        UE_LOG(LogTemp, Warning, TEXT("%s has no Counter Impact notify - counter damage will land at the end of the animation"),
            *CounterMontage->GetName());
    }

    MotionWarping = OwnerCharacter->FindComponentByClass<UMotionWarpingComponent>();
    if (bUseMotionWarping && !MotionWarping)
    {
        UE_LOG(LogTemp, Warning, TEXT("bUseMotionWarping is set but %s has no MotionWarpingComponent - using code-driven approach"),
            *OwnerCharacter->GetName());
    }
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndHitPause();
    EndFinalBlow();
    Super::EndPlay(EndPlayReason);
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!OwnerCharacter) return;

    // Combo meter runs down between hits (frozen during hit-pause like everything else on the player)
    if (ComboCount > 0)
    {
        ComboTimeLeft -= DeltaTime;
        if (ComboTimeLeft <= 0.f)
        {
            BreakCombo();
        }
    }

    if (CombatState == ECombatState::Countering)
    {
        UpdateCounter(DeltaTime);
    }

    if (CombatState == ECombatState::Attacking)
    {
        ActiveAttack.Elapsed += DeltaTime;

        // Travel and turn until the moment of contact, whatever phase the hit window is in
        if (!ActiveAttack.bContactReached)
        {
            UpdateFacing(DeltaTime);
            UpdateApproach(DeltaTime);
        }

        // Window timed around contact when notifies aren't driving it
        if (!ActiveAttack.bHasHitNotifies)
        {
            if (AttackPhase == EAttackPhase::Windup &&
                ActiveAttack.Elapsed >= ActiveAttack.ContactTime - HitWindowLead)
            {
                OpenHitWindow();
            }
            else if (AttackPhase == EAttackPhase::Active &&
                ActiveAttack.Elapsed >= ActiveAttack.ContactTime + DefaultActiveDuration)
            {
                CloseHitWindow();
            }
        }

        if (AttackPhase == EAttackPhase::Active)
        {
            UpdateHitDetection();
        }

        if (!ActiveAttack.bContactReached && ActiveAttack.Elapsed >= ActiveAttack.ContactTime)
        {
            ActiveAttack.bContactReached = true;
            OnContact();
        }

        if (AttackPhase == EAttackPhase::Recovery)
        {
            ActiveAttack.RecoveryElapsed += DeltaTime;
        }

        // Next hit wanted (single-press combo, or a press already made). When it's the following section of the same
        // animation, link the sections so the montage plays straight on - restarting it would blend the next swing away.
        const bool bWantsNextHit = ActiveAttack.bAutoContinue || bAttackBuffered;
        const UAttackData* FlowingAttack = bWantsNextHit ? GetNextFlowingAttack() : nullptr;
        UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
        UAnimMontage* PlayingMontage = const_cast<UAnimMontage*>(ActiveAttack.Montage);

        if (FlowingAttack && AnimInstance)
        {
            AnimInstance->Montage_SetNextSection(ActiveAttack.Section, FlowingAttack->Section, PlayingMontage);
            if (AnimInstance->Montage_GetCurrentSection(PlayingMontage) == FlowingAttack->Section)
            {
                StartAttack(true);
            }
        }
        else if (ActiveAttack.bAutoContinue && ActiveAttack.bContactReached &&
            ActiveAttack.Elapsed >= ActiveAttack.SectionEnd - ActiveAttack.SectionStart - AutoContinueLead)
        {
            // Single-press combo whose next hit is a different montage - start it as this one ends
            StartAttack();
        }
    }

    UpdateTargetHighlight();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void UCombatComponent::TryAttack()
{
    if (!OwnerCharacter || OwnerCharacter->IsDead()) return;

    switch (CombatState)
    {
    case ECombatState::Idle:
        StartAttack();
        break;

    case ECombatState::Attacking:
        // Recovery is cancelable, earlier phases buffer the press until the hit window closes.
        // A single-press combo plays out first - the press waits for its last hit.
        if (AttackPhase == EAttackPhase::Recovery && !ActiveAttack.bAutoContinue && !GetNextFlowingAttack())
        {
            StartAttack();
        }
        else
        {
            bAttackBuffered = true;
        }
        break;

    case ECombatState::Countering:
        // Fires as soon as the counter can be cut after its impact
        bAttackBuffered = true;
        break;

    default:
        // Staggered: no attacking while reeling
        break;
    }
}

void UCombatComponent::SetMovementInput(const FVector& WorldInput)
{
    MovementInput = WorldInput;
    LastMovementInputTime = GetWorld()->GetRealTimeSeconds();

    const bool bWantsToMove = WorldInput.SizeSquared() > 0.25f;

    // Walk out of the tail of a counter
    if (bWantsToMove && CombatState == ECombatState::Countering && CanLeaveCounter())
    {
        EndCounter(true);
    }

    // Walk out of a hit reaction once the stun is over
    if (bWantsToMove && CombatState == ECombatState::Idle && ReactionMontage.IsValid())
    {
        if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance())
        {
            UE_LOG(LogTemp, Log, TEXT("Walked out of reaction %s"), *ReactionMontage->GetName());
            AnimInstance->Montage_Stop(0.2f, ReactionMontage.Get());
        }
        ReactionMontage.Reset();
    }

    if (bAllowMovementCancel &&
        CombatState == ECombatState::Attacking &&
        !ActiveAttack.bAutoContinue &&
        AttackPhase == EAttackPhase::Recovery &&
        !bAttackBuffered &&
        ActiveAttack.RecoveryElapsed >= MovementCancelDelay &&
        WorldInput.SizeSquared() > 0.25f)
    {
        CancelAttack();
    }
}

bool UCombatComponent::HasRecentMovementInput() const
{
    // Move and Attack are separate input actions - either can be processed first in a frame
    return LastMovementInputTime >= 0.0 &&
        GetWorld()->GetRealTimeSeconds() - LastMovementInputTime < 0.1 &&
        MovementInput.SizeSquared() > 0.04f;
}

// ---------------------------------------------------------------------------
// Attack Flow
// ---------------------------------------------------------------------------

void UCombatComponent::StartAttack(bool bFlowOn)
{
    if (ComboAttacks.IsEmpty()) return;

    UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
    if (!AnimInstance) return;

    // Inside a single-press combo the hits play as authored: no flashy swaps, no lunging at someone far off,
    // and a press made during it stays buffered for after its last hit
    const bool bMidSpecialCombo = CombatState == ECombatState::Attacking && ActiveAttack.bAutoContinue;

    GetWorld()->GetTimerManager().ClearTimer(ComboResetTimer);
    if (!bMidSpecialCombo)
    {
        bAttackBuffered = false;
    }
    ReactionMontage.Reset();

    AEnemyCharacter* Target = SelectTarget();
    if (bMidSpecialCombo && Target &&
        FVector::Dist2D(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()) > CloseAttackRange)
    {
        Target = nullptr;
    }
    CurrentTarget = Target;

    // Nobody to hit - swing toward the stick instead of wherever we happened to face
    if (!Target && !bMidSpecialCombo && HasRecentMovementInput())
    {
        OwnerCharacter->SetActorRotation(FRotator(0.f, MovementInput.Rotation().Yaw, 0.f));
    }

    const float TargetDistance = Target
        ? FVector::Dist2D(OwnerCharacter->GetActorLocation(), Target->GetActorLocation())
        : 0.f;

    // Choose the attack:
    //   new target mid-chain or far away   -> flashy attack, restarts the combo
    //   otherwise                          -> next attack in the current combo chain
    //   ...and if the last one will kill   -> flashy attack as the finisher
    if (ChainIndex == 0)
    {
        PickComboChain();
    }
    const TArray<TObjectPtr<UAttackData>>& Chain = GetActiveChain();
    ChainIndex %= Chain.Num();
    const UAttackData* ComboAttack = Chain[ChainIndex];
    const bool bHasFlashy = !FlashyAttacks.IsEmpty();

    const bool bSwitchedTarget = bFlashyKickOnTargetSwitch && Target && bChainHasTarget && Target != LastAttackTarget.Get();
    const bool bFarTarget = Target && TargetDistance > CloseAttackRange;
    const bool bIsComboFinisher = ChainIndex == Chain.Num() - 1;
    const bool bFinisherKills = bFlashyKickOnKillingFinisher && Target && ComboAttack &&
        Target->GetCurrentHealth() <= ComboAttack->Hit.Damage;

    FActiveAttack NewAttack;
    NewAttack.Target = Target;
    NewAttack.bIsTransition = !bMidSpecialCombo && bHasFlashy && (bSwitchedTarget || bFarTarget);
    NewAttack.bIsFinisher = !NewAttack.bIsTransition && bIsComboFinisher;
    NewAttack.bIsFlashy = NewAttack.bIsTransition ||
        (!bMidSpecialCombo && !IsActiveChainSinglePress() && NewAttack.bIsFinisher && bFinisherKills && bHasFlashy);
    NewAttack.bAutoContinue = !NewAttack.bIsFlashy && IsActiveChainSinglePress() && !bIsComboFinisher;

    if (NewAttack.bIsFlashy)
    {
        NewAttack.Data = PickFlashyAttack();
        ChainIndex = 0;
    }
    else
    {
        NewAttack.Data = ComboAttack;
        ChainIndex = (ChainIndex + 1) % Chain.Num();
    }

    if (!NewAttack.Data || !NewAttack.Data->Montage)
    {
        UE_LOG(LogTemp, Warning, TEXT("CombatComponent: an attack in the list is empty or has no montage"));
        ActiveAttack.bAutoContinue = false; // let a running special combo end instead of retrying every frame
        return;
    }

    if (Target)
    {
        LastAttackTarget = Target;
        bChainHasTarget = true;
    }

    UAnimMontage* Montage = NewAttack.Data->Montage;
    NewAttack.Montage = Montage;
    NewAttack.Section = NewAttack.Data->Section;
    NewAttack.StrikeDistance = NewAttack.Data->FallbackStrikeDistance;

    NewAttack.SectionEnd = Montage->GetPlayLength();
    if (!StrikeAnalysis::GetSectionRange(Montage, NewAttack.Section, NewAttack.SectionStart, NewAttack.SectionEnd))
    {
        UE_LOG(LogTemp, Warning, TEXT("Section '%s' not found in montage '%s' (attack %s)"),
            *NewAttack.Section.ToString(), *Montage->GetName(), *NewAttack.Data->GetName());
    }

    // Contact timing and striking limb: read from the animation, else from EnableHitbox notifies
    const FStrikeInfo Strike = bAutoDetectStrike ? AnalyzeStrike(Montage, NewAttack.Section) : FStrikeInfo();
    NewAttack.Socket = TEXT("hand_r");

    if (Strike.bValid)
    {
        NewAttack.ContactTime = Strike.ContactTime;
        NewAttack.Socket = Strike.Bone;
        NewAttack.bHasHitNotifies = false;

        // Stand where the limb at full extension lands just inside the target
        if (bAutoStrikeDistance && Target)
        {
            NewAttack.StrikeDistance = FMath::Max(
                Strike.Reach + Target->GetSimpleCollisionRadius() - StrikePenetration, MinStrikeDistance);
        }
    }
    else
    {
        float ContactTime = DefaultContactTime;
        NewAttack.bHasHitNotifies = FindContactTime(Montage, NewAttack.Section, ContactTime);
        NewAttack.ContactTime = ContactTime;
    }

    if (!NewAttack.Data->HitSocketOverride.IsNone())
    {
        NewAttack.Socket = NewAttack.Data->HitSocketOverride;
    }

    // Big travelling moves keep their root motion, bent onto the target
    NewAttack.bUseWarp = bUseMotionWarping && MotionWarping && Target && Strike.bValid &&
        Strike.RootTravelToContact >= MinWarpRootTravel;

    // Already playing on into this section: keep that montage instance, the swing is under way
    const bool bKeepPlaying = bFlowOn && Montage == ActiveAttack.Montage &&
        AnimInstance->Montage_GetCurrentSection(Montage) == NewAttack.Section;

    // Otherwise playing a new montage interrupts the previous one; its end delegate is ignored via instance ID
    if (!bKeepPlaying && AnimInstance->Montage_Play(Montage, 1.f) <= 0.f)
    {
        ActiveAttack.bAutoContinue = false;
        return;
    }
    if (!NewAttack.Section.IsNone())
    {
        if (!bKeepPlaying)
        {
            AnimInstance->Montage_JumpToSection(NewAttack.Section, Montage);
        }
        AnimInstance->Montage_SetNextSection(NewAttack.Section, NAME_None, Montage);
    }

    if (const FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(Montage))
    {
        NewAttack.MontageInstanceID = Instance->GetInstanceID();
    }

    FOnMontageEnded EndDelegate = FOnMontageEnded::CreateUObject(
        this, &UCombatComponent::OnAttackMontageEnded, NewAttack.MontageInstanceID);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

    ActiveAttack = MoveTemp(NewAttack);
    CombatState = ECombatState::Attacking;
    AttackPhase = EAttackPhase::Windup;

    LockMovement();
    AddWarpModifiers();
}

FStrikeInfo UCombatComponent::AnalyzeStrike(const UAnimMontage* Montage, FName Section)
{
    const TPair<const UAnimMontage*, FName> CacheKey(Montage, Section);
    if (const FStrikeInfo* Cached = StrikeCache.Find(CacheKey))
    {
        return *Cached;
    }

    FStrikeInfo Info;
    float SectionStart = 0.f;
    float SectionEnd = 0.f;
    if (StrikeAnalysis::GetSectionRange(Montage, Section, SectionStart, SectionEnd))
    {
        // Character forward in mesh component space (+Y for the mannequin, whose mesh is rotated -90)
        const FVector Forward = OwnerCharacter->GetMesh()->GetRelativeRotation().UnrotateVector(FVector::ForwardVector);
        Info = StrikeAnalysis::Analyze(Montage, SectionStart, SectionEnd, StrikeBones, Forward, ContactExtension);
    }

    if (Info.bValid)
    {
        UE_LOG(LogTemp, Log, TEXT("Strike %s.%s: %s at %.2fs, reach %.0f, root travel %.0f"),
            *Montage->GetName(), *Section.ToString(), *Info.Bone.ToString(), Info.ContactTime, Info.Reach, Info.RootTravelToContact);
    }

    StrikeCache.Add(CacheKey, Info);
    return Info;
}

void UCombatComponent::PickComboChain()
{
    // Combo Attacks is the main combo; each extra chain gets its own roll to take over this time
    ActiveChain = 0;
    for (int32 i = 0; i < ExtraComboChains.Num(); ++i)
    {
        const FComboChain& Chain = ExtraComboChains[i];
        if (!Chain.Attacks.IsEmpty() && FMath::FRand() < Chain.Chance)
        {
            ActiveChain = i + 1;
            return;
        }
    }
}

const TArray<TObjectPtr<UAttackData>>& UCombatComponent::GetActiveChain() const
{
    return ExtraComboChains.IsValidIndex(ActiveChain - 1) ? ExtraComboChains[ActiveChain - 1].Attacks : ComboAttacks;
}

bool UCombatComponent::IsActiveChainSinglePress() const
{
    return ExtraComboChains.IsValidIndex(ActiveChain - 1) && ExtraComboChains[ActiveChain - 1].bPlayOnSinglePress;
}

const UAttackData* UCombatComponent::GetNextFlowingAttack() const
{
    // Only mid-chain, never out of a flashy attack (that restarts the chain)
    if (CombatState != ECombatState::Attacking || ActiveAttack.bIsFlashy || !ActiveAttack.Montage || ChainIndex == 0)
    {
        return nullptr;
    }

    const TArray<TObjectPtr<UAttackData>>& Chain = GetActiveChain();
    const UAttackData* Next = Chain.IsValidIndex(ChainIndex) ? Chain[ChainIndex].Get() : nullptr;
    if (!Next || Next->Montage != ActiveAttack.Montage)
    {
        return nullptr;
    }

    // Has to be the very next section...
    float NextStart = 0.f;
    float NextEnd = 0.f;
    if (!StrikeAnalysis::GetSectionRange(ActiveAttack.Montage, Next->Section, NextStart, NextEnd) ||
        !FMath::IsNearlyEqual(NextStart, ActiveAttack.SectionEnd, 0.01f))
    {
        return nullptr;
    }

    // ...cut from the same animation, so the pose at the cut carries straight on. Sections that are separate
    // animations placed back to back (like the default combo) restart with a blend instead.
    const UAnimMontage* Montage = ActiveAttack.Montage;
    if (Montage->SlotAnimTracks.IsEmpty())
    {
        return nullptr;
    }
    const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
    const int32 SegmentBefore = Track.GetSegmentIndexAtTime(NextStart - 0.01f);
    return SegmentBefore != INDEX_NONE && SegmentBefore == Track.GetSegmentIndexAtTime(NextStart + 0.01f) ? Next : nullptr;
}

const UAttackData* UCombatComponent::PickFlashyAttack()
{
    if (FlashyAttacks.IsEmpty()) return nullptr;

    // Avoid playing the same one twice in a row
    int32 Index = FMath::RandRange(0, FlashyAttacks.Num() - 1);
    if (FlashyAttacks.Num() > 1 && Index == LastFlashyKickIndex)
    {
        Index = (Index + 1) % FlashyAttacks.Num();
    }
    LastFlashyKickIndex = Index;

    return FlashyAttacks[Index];
}

void UCombatComponent::OpenHitWindow()
{
    if (AttackPhase != EAttackPhase::Windup) return;

    AttackPhase = EAttackPhase::Active;
    ActiveAttack.HitActors.Reset();
    ActiveAttack.bHasLastSocketLocation = false;

    UpdateHitDetection();
}

void UCombatComponent::CloseHitWindow()
{
    if (AttackPhase != EAttackPhase::Active) return;

    // Fallback for notify-driven windows that close before the contact frame
    ApplyGuaranteedHit(ActiveAttack.Target.Get());

    AttackPhase = EAttackPhase::Recovery;
    ActiveAttack.RecoveryElapsed = 0.f;

    // A hit that flows on from this animation waits for the section to end instead (see TickComponent)
    if (bAttackBuffered && !ActiveAttack.bAutoContinue && !GetNextFlowingAttack())
    {
        StartAttack();
    }
}

void UCombatComponent::FinishAttack()
{
    AttackPhase = EAttackPhase::None;
    CombatState = ECombatState::Idle;
    ActiveAttack = FActiveAttack();
    bAttackBuffered = false;

    ClearWarpModifiers();
    UnlockMovement();

    GetWorld()->GetTimerManager().SetTimer(
        ComboResetTimer, this, &UCombatComponent::ResetCombo, ComboWindowDuration, false);
}

void UCombatComponent::CancelAttack()
{
    if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance())
    {
        AnimInstance->Montage_Stop(0.2f, const_cast<UAnimMontage*>(ActiveAttack.Montage));
    }

    FinishAttack();
}

void UCombatComponent::ResetCombo()
{
    // The attack chain only - the combo meter keeps counting until it times out or we get hit
    ChainIndex = 0;
    LastAttackTarget.Reset();
    bChainHasTarget = false;
}

void UCombatComponent::AddComboHit()
{
    ++ComboCount;
    ComboTimeLeft = ComboTimeout;
    OnComboChanged.Broadcast(ComboCount);
}

void UCombatComponent::BreakCombo()
{
    if (ComboCount == 0) return;

    ComboCount = 0;
    ComboTimeLeft = 0.f;
    OnComboChanged.Broadcast(0);
}

void UCombatComponent::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 MontageInstanceID)
{
    // Stale callback from an attack we already chained out of
    if (CombatState != ECombatState::Attacking || MontageInstanceID != ActiveAttack.MontageInstanceID) return;

    // Montage ended without a DisableHitbox notify - close the window now (may chain a buffered attack)
    if (AttackPhase == EAttackPhase::Active)
    {
        CloseHitWindow();
        if (MontageInstanceID != ActiveAttack.MontageInstanceID) return;
    }

    if (bAttackBuffered)
    {
        StartAttack();
        return;
    }

    FinishAttack();
}

bool UCombatComponent::IsCurrentAttackNotify(const UAnimSequenceBase* Animation, int32 MontageInstanceID) const
{
    // Auto-detected attacks time their own window around contact
    if (CombatState != ECombatState::Attacking || !ActiveAttack.bHasHitNotifies) return false;

    if (MontageInstanceID != INDEX_NONE)
    {
        return MontageInstanceID == ActiveAttack.MontageInstanceID;
    }

    return Animation == ActiveAttack.Montage;
}

void UCombatComponent::EnableHitbox(const UAnimSequenceBase* Animation, int32 MontageInstanceID)
{
    if (!IsCurrentAttackNotify(Animation, MontageInstanceID)) return;
    OpenHitWindow();
}

void UCombatComponent::DisableHitbox(const UAnimSequenceBase* Animation, int32 MontageInstanceID)
{
    if (!IsCurrentAttackNotify(Animation, MontageInstanceID)) return;
    CloseHitWindow();
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void UCombatComponent::UpdateFacing(float DeltaTime)
{
    // The warp rotates us as part of the root motion
    if (ActiveAttack.bUseWarp) return;

    const AEnemyCharacter* Target = ActiveAttack.Target.Get();
    if (!Target) return;

    FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
    ToTarget.Z = 0.f;
    if (ToTarget.IsNearlyZero()) return;

    OwnerCharacter->SetActorRotation(FMath::RInterpTo(
        OwnerCharacter->GetActorRotation(), ToTarget.Rotation(), DeltaTime, TurnSpeed));
}

void UCombatComponent::UpdateApproach(float DeltaTime)
{
    if (ActiveAttack.bUseWarp)
    {
        UpdateWarpTarget();
        return;
    }

    const AEnemyCharacter* Target = ActiveAttack.Target.Get();
    if (!Target || DeltaTime <= 0.f) return;

    const FVector OwnerLocation = OwnerCharacter->GetActorLocation();
    FVector ToTarget = Target->GetActorLocation() - OwnerLocation;
    ToTarget.Z = 0.f;

    const float Distance = ToTarget.Size();
    if (Distance < KINDA_SMALL_NUMBER) return;

    // Home in on the live target position so we arrive exactly at the contact frame
    const float Remaining = Distance - ActiveAttack.StrikeDistance;
    const float TimeLeft = FMath::Max(ActiveAttack.ContactTime - ActiveAttack.Elapsed, DeltaTime);
    const float MaxStep = MaxApproachSpeed * DeltaTime;
    const float Step = FMath::Clamp(Remaining * (DeltaTime / TimeLeft), -MaxStep, MaxStep);

    OwnerCharacter->SetActorLocation(OwnerLocation + (ToTarget / Distance) * Step, true);
}

void UCombatComponent::UpdateWarpTarget()
{
    if (!ActiveAttack.bUseWarp || !MotionWarping) return;

    // Target gone mid-lunge - the warp window just plays the authored root motion
    const AEnemyCharacter* Target = ActiveAttack.Target.Get();
    if (!Target) return;

    FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
    ToTarget.Z = 0.f;
    const FVector Direction = ToTarget.GetSafeNormal();

    FVector WarpLocation = Target->GetActorLocation() - Direction * ActiveAttack.StrikeDistance;
    WarpLocation.Z = OwnerCharacter->GetActorLocation().Z;

    MotionWarping->AddOrUpdateWarpTargetFromLocationAndRotation(
        WarpTargetName, WarpLocation, Direction.Rotation());
}

void UCombatComponent::AddWarpModifiers()
{
    ClearWarpModifiers();
    if (!ActiveAttack.bUseWarp) return;

    UpdateWarpTarget();

    // Windows are in montage time: bend the root motion onto the target up to contact,
    // then damp it so the follow-through doesn't carry us into (or through) the enemy
    const float ContactPosition = ActiveAttack.SectionStart + ActiveAttack.ContactTime;

    WarpModifiers.Add(URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(
        MotionWarping, ActiveAttack.Montage, ActiveAttack.SectionStart, ContactPosition,
        WarpTargetName, EWarpPointAnimProvider::None, FTransform::Identity, NAME_None));

    WarpModifiers.Add(URootMotionModifier_Scale::AddRootMotionModifierScale(
        MotionWarping, ActiveAttack.Montage, ContactPosition, ActiveAttack.SectionEnd,
        FVector(PostContactRootMotionScale, PostContactRootMotionScale, 1.f)));
}

void UCombatComponent::ClearWarpModifiers()
{
    // Chained attacks reuse the same montage, so stale windows from the last attack must go
    for (const TWeakObjectPtr<URootMotionModifier>& Modifier : WarpModifiers)
    {
        if (Modifier.IsValid())
        {
            Modifier->SetState(ERootMotionModifierState::MarkedForRemoval);
        }
    }
    WarpModifiers.Reset();

    if (MotionWarping)
    {
        MotionWarping->RemoveWarpTarget(WarpTargetName);
    }
}

void UCombatComponent::LockMovement()
{
    UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
    Movement->StopMovementImmediately();

    // Warped attacks and swings at nobody move with the animation's root motion, which needs walking;
    // the code-driven approach disables movement, which also discards that root motion
    if (ActiveAttack.bUseWarp || !ActiveAttack.Target.Get())
    {
        if (Movement->MovementMode == MOVE_None)
        {
            Movement->SetMovementMode(MOVE_Walking);
        }
    }
    else
    {
        Movement->DisableMovement();
    }
}

void UCombatComponent::UnlockMovement()
{
    if (OwnerCharacter->IsDead()) return;

    UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
    if (Movement->MovementMode == MOVE_None)
    {
        Movement->SetMovementMode(MOVE_Walking);
    }
}

// ---------------------------------------------------------------------------
// Hit Detection
// ---------------------------------------------------------------------------

float UCombatComponent::GetHitRadius() const
{
    return ActiveAttack.Socket.ToString().StartsWith(TEXT("foot")) ? FootHitboxRadius : HitboxRadius;
}

void UCombatComponent::UpdateHitDetection()
{
    const FVector SocketLocation = OwnerCharacter->GetMesh()->GetSocketLocation(ActiveAttack.Socket);
    const FVector SweepStart = ActiveAttack.bHasLastSocketLocation ? ActiveAttack.LastSocketLocation : SocketLocation;
    ActiveAttack.LastSocketLocation = SocketLocation;
    ActiveAttack.bHasLastSocketLocation = true;

    const float Radius = GetHitRadius();

    if (bDrawHitDebug)
    {
        DrawDebugCapsule(GetWorld(), (SweepStart + SocketLocation) * 0.5f,
            FVector::Dist(SweepStart, SocketLocation) * 0.5f + Radius, Radius,
            FRotationMatrix::MakeFromZ(SocketLocation - SweepStart).ToQuat(),
            FColor::Red, false, 0.5f);
    }

    // Sweep from last frame's socket position so fast swings can't tunnel through the target
    TArray<FHitResult> Hits;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatHitSweep), false, OwnerCharacter);
    GetWorld()->SweepMultiByObjectType(Hits, SweepStart, SocketLocation, FQuat::Identity,
        FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);

    AEnemyCharacter* Target = ActiveAttack.Target.Get();

    for (const FHitResult& Hit : Hits)
    {
        AArkhamCharacterBase* Victim = Cast<AArkhamCharacterBase>(Hit.GetActor());
        if (!Victim || Victim->IsDead()) continue;
        if (ActiveAttack.HitActors.Contains(Victim)) continue;

        // Attacks marked OnlyHitsTarget (knockdown kicks) never clip bystanders
        if (ActiveAttack.Data->bOnlyHitsTarget && Target && Victim != Target) continue;

        // Hits that start inside the capsule have no meaningful impact point
        ApplyHit(Victim, Hit.bStartPenetrating ? SocketLocation : FVector(Hit.ImpactPoint));
    }
}

void UCombatComponent::OnContact()
{
    // The limb is at full extension - make the hit land now, in sync with the animation
    ApplyGuaranteedHit(ActiveAttack.Target.Get());
}

void UCombatComponent::ApplyGuaranteedHit(AEnemyCharacter* Target)
{
    // Freeflow never whiffs on the committed target when it's in reach
    if (!bGuaranteeTargetHit || ActiveAttack.bTargetHit || !IsTargetInReach(Target)) return;

    // No real contact point - use the target's surface facing us, at the height of the striking limb
    FVector ToOwner = OwnerCharacter->GetActorLocation() - Target->GetActorLocation();
    ToOwner.Z = 0.f;
    FVector ImpactPoint = Target->GetActorLocation() + ToOwner.GetSafeNormal() * Target->GetSimpleCollisionRadius();
    ImpactPoint.Z = OwnerCharacter->GetMesh()->GetSocketLocation(ActiveAttack.Socket).Z;

    ApplyHit(Target, ImpactPoint);
}

bool UCombatComponent::IsTargetInReach(const AEnemyCharacter* Target) const
{
    if (!Target || Target->IsDead()) return false;

    FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
    ToTarget.Z = 0.f;

    const bool bInRange = ToTarget.Size() <= ActiveAttack.StrikeDistance + TargetHitTolerance;
    const bool bFacing = FVector::DotProduct(OwnerCharacter->GetActorForwardVector(), ToTarget.GetSafeNormal()) > 0.5f;
    return bInRange && bFacing;
}

void UCombatComponent::ApplyHit(AArkhamCharacterBase* Victim, const FVector& ImpactPoint)
{
    ActiveAttack.HitActors.Add(Victim);
    if (Victim == ActiveAttack.Target.Get())
    {
        ActiveAttack.bTargetHit = true;
    }

    // Everything about how hard this lands comes from the attack's data
    const FCombatHit& Hit = ActiveAttack.Data->Hit;

    AddComboHit();
    Victim->TakeCombatHit(Hit, OwnerCharacter);
    PlayHitImpact(ImpactPoint, Hit.CameraShakeScale);

    // Knocked down or out - let go immediately so the next press (or a buffered one) goes to someone else
    if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
        Enemy && Enemy == CurrentTarget.Get() && (Enemy->IsDead() || Enemy->IsStaggered()))
    {
        CurrentTarget = nullptr;
    }

    // The slow-mo replaces the freeze frame on the fight's last hit
    if (Victim->IsDead() && IsLastEnemyStanding(Victim))
    {
        StartFinalBlow(Victim);
        return;
    }

    TriggerHitPause(Victim, Hit.HitPauseDuration);
}

// ---------------------------------------------------------------------------
// Impact
// ---------------------------------------------------------------------------

void UCombatComponent::PlayHitImpact(const FVector& ImpactPoint, float ShakeScale)
{
    if (HitCameraShake)
    {
        if (APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController()))
        {
            PC->ClientStartCameraShake(HitCameraShake, ShakeScale);
        }
    }

    if (HitEffect)
    {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitEffect, ImpactPoint, OwnerCharacter->GetActorRotation());
    }

    if (HitSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, HitSound, ImpactPoint);
    }
}

bool UCombatComponent::IsLastEnemyStanding(const AActor* Victim) const
{
    // More enemies still on the way - this isn't the end of the fight
    for (TActorIterator<AEnemySpawner> It(GetWorld()); It; ++It)
    {
        if (!It->IsEncounterComplete()) return false;
    }

    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        if (*It != Victim && !It->IsDead()) return false;
    }
    return true;
}

void UCombatComponent::StartFinalBlow(AActor* Victim)
{
    if (bSlowMoOnFinalBlow && FinalBlowTimeDilation > 0.f)
    {
        EndHitPause();
        bFinalBlowActive = true;
        UGameplayStatics::SetGlobalTimeDilation(this, FinalBlowTimeDilation);

        // Timers run in dilated time, so scale to get real seconds
        GetWorld()->GetTimerManager().SetTimer(FinalBlowTimer, this, &UCombatComponent::EndFinalBlow,
            FinalBlowDuration * FinalBlowTimeDilation, false);
    }

    OnFinalBlow.Broadcast(Victim);
}

void UCombatComponent::EndFinalBlow()
{
    if (!bFinalBlowActive) return;

    bFinalBlowActive = false;
    GetWorld()->GetTimerManager().ClearTimer(FinalBlowTimer);
    UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
}

// ---------------------------------------------------------------------------
// Counter
// ---------------------------------------------------------------------------

void UCombatComponent::TryCounter()
{
    if (!OwnerCharacter || OwnerCharacter->IsDead() || !CounterAttack || !CounterAttack->Montage) return;
    if (CombatState == ECombatState::Staggered) return;
    if (CombatState == ECombatState::Countering && !CanLeaveCounter()) return;

    // The punch that lands soonest is the one to answer
    AEnemyCharacter* BestEnemy = nullptr;
    float BestTimeToContact = TNumericLimits<float>::Max();

    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        AEnemyCharacter* Enemy = *It;
        if (!IsValid(Enemy) || Enemy->IsDead() || !Enemy->IsCounterable()) continue;
        if (FVector::Dist2D(OwnerCharacter->GetActorLocation(), Enemy->GetActorLocation()) > CounterRange) continue;

        const float TimeToContact = Enemy->GetTimeToContact();
        if (TimeToContact < BestTimeToContact)
        {
            BestTimeToContact = TimeToContact;
            BestEnemy = Enemy;
        }
    }

    if (BestEnemy)
    {
        StartCounter(BestEnemy);
    }
}

void UCombatComponent::StartCounter(AEnemyCharacter* Enemy)
{
    // Drop whatever we were doing - countering always takes priority
    CancelActiveAttack();
    GetWorld()->GetTimerManager().ClearTimer(HitStunTimer);
    GetWorld()->GetTimerManager().ClearTimer(ComboResetTimer);

    CombatState = ECombatState::Countering;
    bAttackBuffered = false;
    CounterTarget = Enemy;
    CounterElapsed = 0.f;
    bCounterImpactDone = false;

    // Line the pair up the way the paired animation was authored: the enemy turns to us, we stand CounterPairDistance
    // in front of it and CounterPairSideOffset to its right, facing it. The camera lag hides the correction.
    FVector FromEnemy = OwnerCharacter->GetActorLocation() - Enemy->GetActorLocation();
    FromEnemy.Z = 0.f;
    FromEnemy = FromEnemy.IsNearlyZero() ? Enemy->GetActorForwardVector() : FromEnemy.GetSafeNormal();

    const FRotator EnemyFacing = FromEnemy.Rotation();
    Enemy->SetActorRotation(EnemyFacing);

    FVector PairLocation = Enemy->GetActorLocation() + FromEnemy * CounterPairDistance +
        FRotationMatrix(EnemyFacing).GetUnitAxis(EAxis::Y) * CounterPairSideOffset;
    PairLocation.Z = OwnerCharacter->GetActorLocation().Z;
    OwnerCharacter->SetActorLocationAndRotation(PairLocation, (-FromEnemy).Rotation());

    // Both step into each other during the animation; blocked root motion is what pulls the pair out of sync
    SetIgnoreCounterPartner(Enemy, true);

    // Both halves of the paired animation move with root motion
    UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
    Movement->StopMovementImmediately();
    UnlockMovement();

    Enemy->PlayCountered(OwnerCharacter);
    PlayReaction(CounterAttack->Montage);
    CounterLength = CounterAttack->Montage->GetPlayLength();

    // Keep fighting the same enemy after the counter
    CurrentTarget = Enemy;
    LastAttackTarget = Enemy;
    bChainHasTarget = true;
    ChainIndex = 0;
}

void UCombatComponent::SetIgnoreCounterPartner(AEnemyCharacter* Enemy, bool bIgnore)
{
    if (!IsValid(Enemy)) return;

    if (bIgnore)
    {
        OwnerCharacter->MoveIgnoreActorAdd(Enemy);
        Enemy->MoveIgnoreActorAdd(OwnerCharacter);
    }
    else
    {
        OwnerCharacter->MoveIgnoreActorRemove(Enemy);
        Enemy->MoveIgnoreActorRemove(OwnerCharacter);
    }
}

bool UCombatComponent::IsEngaging(const AEnemyCharacter* Enemy) const
{
    if (!Enemy) return false;

    return (CombatState == ECombatState::Attacking && ActiveAttack.Target.Get() == Enemy) ||
        (CombatState == ECombatState::Countering && CounterTarget.Get() == Enemy) ||
        (bChainHasTarget && LastAttackTarget.Get() == Enemy);
}

bool UCombatComponent::CanLeaveCounter() const
{
    return bCounterImpactDone && CounterElapsed >= CounterImpactElapsed + CounterCancelDelay;
}

void UCombatComponent::OnCounterImpact(const UAnimSequenceBase* Animation)
{
    // Only the counter we're currently playing may land its hit, and only once
    if (CombatState != ECombatState::Countering || bCounterImpactDone || !CounterAttack || Animation != CounterAttack->Montage) return;

    ApplyCounterImpact();
}

void UCombatComponent::ApplyCounterImpact()
{
    bCounterImpactDone = true;
    CounterImpactElapsed = CounterElapsed;

    AEnemyCharacter* Enemy = CounterTarget.Get();
    if (!Enemy || Enemy->IsDead()) return;

    Enemy->TakeCounterHit(CounterAttack->Hit, OwnerCharacter);
    AddComboHit();

    FVector ToOwner = OwnerCharacter->GetActorLocation() - Enemy->GetActorLocation();
    ToOwner.Z = 0.f;
    const FVector ImpactPoint = Enemy->GetActorLocation() +
        ToOwner.GetSafeNormal() * Enemy->GetSimpleCollisionRadius() + FVector(0.f, 0.f, 40.f);
    PlayHitImpact(ImpactPoint, CounterAttack->Hit.CameraShakeScale);

    if (Enemy->IsDead() && IsLastEnemyStanding(Enemy))
    {
        StartFinalBlow(Enemy);
    }
    else
    {
        TriggerHitPause(Enemy, CounterAttack->Hit.HitPauseDuration);
    }
}

void UCombatComponent::UpdateCounter(float DeltaTime)
{
    CounterElapsed += DeltaTime;

    if (bAttackBuffered && CanLeaveCounter())
    {
        EndCounter(false);
        StartAttack();
        return;
    }

    if (CounterElapsed >= CounterLength)
    {
        // Montage has no Counter Impact notify - still land the hit so the counter isn't wasted
        if (!bCounterImpactDone)
        {
            ApplyCounterImpact();
        }
        EndCounter(false);
    }
}

void UCombatComponent::EndCounter(bool bStopAnimation)
{
    if (bStopAnimation && ReactionMontage.IsValid())
    {
        if (UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance())
        {
            AnimInstance->Montage_Stop(0.2f, ReactionMontage.Get());
        }
    }
    ReactionMontage.Reset();

    CombatState = ECombatState::Idle;
    SetIgnoreCounterPartner(CounterTarget.Get(), false);
    CounterTarget.Reset();
    bAttackBuffered = false;
    UnlockMovement();

    GetWorld()->GetTimerManager().SetTimer(
        ComboResetTimer, this, &UCombatComponent::ResetCombo, ComboWindowDuration, false);
}

// ---------------------------------------------------------------------------
// Hit Reactions
// ---------------------------------------------------------------------------

void UCombatComponent::CancelActiveAttack()
{
    if (CombatState == ECombatState::Attacking)
    {
        ClearWarpModifiers();
        ActiveAttack = FActiveAttack();
    }

    AttackPhase = EAttackPhase::None;
    bAttackBuffered = false;
    EndHitPause();
}

void UCombatComponent::PlayReaction(UAnimMontage* Montage)
{
    UAnimInstance* AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
    if (!AnimInstance || !Montage) return;

    // Blend times, slot and auto blend out come from the montage asset
    if (AnimInstance->Montage_Play(Montage) <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("Reaction %s failed to play"), *Montage->GetName());
    }
    ReactionMontage = Montage;
}

void UCombatComponent::ReceiveHit(AActor* Attacker, const FCombatHit& Hit)
{
    if (!OwnerCharacter) return;

    CancelActiveAttack();
    BreakCombo();
    if (CombatState == ECombatState::Countering)
    {
        SetIgnoreCounterPartner(CounterTarget.Get(), false);
        CounterTarget.Reset();
    }
    CombatState = ECombatState::Staggered;

    // Feedback first, so it lands even on the killing blow
    if (PlayerHitCameraShake)
    {
        if (APlayerController* PC = Cast<APlayerController>(OwnerCharacter->GetController()))
        {
            PC->ClientStartCameraShake(PlayerHitCameraShake, Hit.CameraShakeScale);
        }
    }
    TriggerHitPause(Attacker, Hit.HitPauseDuration);

    // Hit from behind with an animation for it: react where we stand. Otherwise face the hit so the reaction plays the right way.
    const bool bDead = OwnerCharacter->IsDead();
    FVector ToAttacker = Attacker ? Attacker->GetActorLocation() - OwnerCharacter->GetActorLocation() : FVector::ZeroVector;
    ToAttacker.Z = 0.f;

    bool bFromBehind = false;
    if (!ToAttacker.IsNearlyZero())
    {
        const bool bHasBehindAnim = bDead ? DeathMontageFromBehind != nullptr : !HitReactMontagesFromBehind.IsEmpty();
        bFromBehind = bHasBehindAnim && FVector::DotProduct(OwnerCharacter->GetActorForwardVector(), ToAttacker.GetSafeNormal()) <
            FMath::Cos(FMath::DegreesToRadians(FromBehindAngle));

        if (!bFromBehind)
        {
            OwnerCharacter->SetActorRotation(ToAttacker.Rotation());
        }
    }

    if (bDead)
    {
        // The death montage has auto blend out off, so it holds the knocked-down pose
        PlayReaction(bFromBehind ? DeathMontageFromBehind : DeathMontage);
        return;
    }

    // Reactions knock back with root motion, which needs walking
    OwnerCharacter->GetCharacterMovement()->StopMovementImmediately();
    UnlockMovement();

    const TArray<TObjectPtr<UAnimMontage>>& Reactions = bFromBehind ? HitReactMontagesFromBehind : HitReactMontages;
    if (!Reactions.IsEmpty())
    {
        PlayReaction(Reactions[FMath::RandRange(0, Reactions.Num() - 1)]);
        UE_LOG(LogTemp, Log, TEXT("Player hit by %s%s, playing %s"), Attacker ? *Attacker->GetName() : TEXT("?"),
            bFromBehind ? TEXT(" from behind") : TEXT(""), ReactionMontage.IsValid() ? *ReactionMontage->GetName() : TEXT("nothing"));
    }

    GetWorld()->GetTimerManager().SetTimer(HitStunTimer, this, &UCombatComponent::EndHitStun, HitStunDuration, false);
}

void UCombatComponent::EndHitStun()
{
    if (CombatState != ECombatState::Staggered || OwnerCharacter->IsDead()) return;

    // Back in control; the reaction keeps playing until the player moves or attacks
    CombatState = ECombatState::Idle;
    GetWorld()->GetTimerManager().SetTimer(
        ComboResetTimer, this, &UCombatComponent::ResetCombo, ComboWindowDuration, false);
}

// ---------------------------------------------------------------------------
// Hit Pause
// ---------------------------------------------------------------------------

void UCombatComponent::TriggerHitPause(AActor* HitActor, float Duration)
{
    if (Duration <= 0.f || !HitActor) return;

    OwnerCharacter->CustomTimeDilation = 0.f;
    HitActor->CustomTimeDilation = 0.f;
    HitPausedActors.AddUnique(HitActor);

    // One timer restores everyone, so a second hit can't strand the first victim frozen
    GetWorld()->GetTimerManager().SetTimer(
        HitPauseTimer, this, &UCombatComponent::EndHitPause, Duration, false);
}

void UCombatComponent::EndHitPause()
{
    if (OwnerCharacter)
    {
        OwnerCharacter->CustomTimeDilation = 1.f;
    }

    for (const TWeakObjectPtr<AActor>& Actor : HitPausedActors)
    {
        if (Actor.IsValid())
        {
            Actor->CustomTimeDilation = 1.f;
        }
    }
    HitPausedActors.Reset();
}

// ---------------------------------------------------------------------------
// Targeting
// ---------------------------------------------------------------------------

bool UCombatComponent::IsTargetable(const AEnemyCharacter* Enemy, bool bAllowStaggered) const
{
    if (!IsValid(Enemy) || Enemy->IsDead()) return false;
    if (Enemy->IsStaggered() && !bAllowStaggered) return false;
    return FVector::Dist2D(OwnerCharacter->GetActorLocation(), Enemy->GetActorLocation()) <= LungeRange;
}

AEnemyCharacter* UCombatComponent::FindTargetInDirection(const FVector& Direction, float MinDot, bool bAllowStaggered) const
{
    FVector FlatDirection = Direction;
    FlatDirection.Z = 0.f;
    FlatDirection.Normalize();

    AEnemyCharacter* BestTarget = nullptr;
    float BestScore = -1.f;

    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        AEnemyCharacter* Enemy = *It;
        if (!IsTargetable(Enemy, bAllowStaggered)) continue;

        FVector ToEnemy = Enemy->GetActorLocation() - OwnerCharacter->GetActorLocation();
        ToEnemy.Z = 0.f;
        const float Distance = ToEnemy.Size();

        const float Dot = FlatDirection.IsNearlyZero() ? 1.f : FVector::DotProduct(FlatDirection, ToEnemy.GetSafeNormal());
        if (Dot < MinDot) continue;

        const float Score = Dot * 0.7f + (1.f - Distance / LungeRange) * 0.3f;
        if (Score > BestScore)
        {
            BestScore = Score;
            BestTarget = Enemy;
        }
    }

    return BestTarget;
}

AEnemyCharacter* UCombatComponent::SelectTarget() const
{
    // Stick direction wins - this is what lets the player freeflow between enemies
    if (HasRecentMovementInput())
    {
        if (AEnemyCharacter* Target = FindTargetInDirection(MovementInput, DirectionalTargetMinDot))
        {
            return Target;
        }
    }

    // No stick input: stay on the current target
    if (AEnemyCharacter* Current = CurrentTarget.Get(); IsTargetable(Current))
    {
        return Current;
    }

    // Otherwise whatever's in front, then anything in range
    if (AEnemyCharacter* Target = FindTargetInDirection(OwnerCharacter->GetActorForwardVector(), DirectionalTargetMinDot))
    {
        return Target;
    }

    if (AEnemyCharacter* Target = FindTargetInDirection(FVector::ZeroVector, -1.f))
    {
        return Target;
    }

    // Only knocked-down enemies left - keep beating on them rather than swinging at air
    return FindTargetInDirection(FVector::ZeroVector, -1.f, true);
}

void UCombatComponent::UpdateTargetHighlight()
{
    // Outline shows who the next press would hit
    AEnemyCharacter* NewHighlight = SelectTarget();
    AEnemyCharacter* OldHighlight = HighlightedTarget.Get();
    if (NewHighlight == OldHighlight) return;

    if (OldHighlight)
    {
        OldHighlight->HideOutline();
    }
    if (NewHighlight)
    {
        NewHighlight->ShowOutline();
    }
    HighlightedTarget = NewHighlight;
}
