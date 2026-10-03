#include "Characters/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "Characters/PlayerCharacter.h"
#include "Characters/ArkhamCharacterBase.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimMontage.h"

AEnemyCharacter::AEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    bUseControllerRotationYaw = false;

    // FacePlayer handles all rotation - AI controller handles position only
    GetCharacterMovement()->bOrientRotationToMovement = false;

    GetMesh()->SetRootBodyIndex(0);

    // --- Attack Indicator ---
    // Positioned and turned to the camera every frame while visible; mesh and material are set in the Blueprint
    AttackIndicatorRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AttackIndicatorRing"));
    AttackIndicatorRing->SetupAttachment(RootComponent);
    AttackIndicatorRing->SetUsingAbsoluteLocation(true);
    AttackIndicatorRing->SetUsingAbsoluteRotation(true);
    AttackIndicatorRing->SetUsingAbsoluteScale(true);
    AttackIndicatorRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttackIndicatorRing->SetCastShadow(false);
    AttackIndicatorRing->SetHiddenInGame(true);

    // Sections that read well as a light hit / knockdown in the enemy montages
    HitReactSections = { TEXT("HitReact1"), TEXT("HitReact3") };
    StaggerSections = { TEXT("StaggerLarge1"), TEXT("StaggerLarge2"), TEXT("StaggerLarge3") };
}

void AEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();

    GetVisibleMesh()->SetOverlayMaterial(nullptr);

    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    // Instance whatever material the Blueprint put on the ring so colour and flashing can be driven per enemy
    if (AttackIndicatorRing->GetMaterial(0))
    {
        IndicatorMID = AttackIndicatorRing->CreateAndSetMaterialInstanceDynamic(0);
    }

    // Stagger start wandering so enemies dont all move simultaneously
    float RandomDelay = FMath::RandRange(0.5f, 2.f);
    GetWorld()->GetTimerManager().SetTimer(
        WanderTimer,
        this,
        &AEnemyCharacter::StartWandering,
        RandomDelay,
        false
    );
}

void AEnemyCharacter::SetAssignedPosition(FVector NewPosition)
{
    AssignedPosition = NewPosition;
    bHasAssignedPosition = true;
}

void AEnemyCharacter::StartWandering()
{
    DoWander();
}

void AEnemyCharacter::DoWander()
{
    if (bIsDead) return;
    if (bIsBeingHit) return;
    if (bIsStaggered) return;
    if (!bHasAssignedPosition) return;

    AAIController* AIController = Cast<AAIController>(GetController());
    if (!AIController) return;

    // Busy attacking - check back later instead of walking off mid-swing
    if (IsAttacking())
    {
        GetWorld()->GetTimerManager().SetTimer(WanderTimer, this, &AEnemyCharacter::DoWander, MinWanderInterval, false);
        return;
    }

    TArray<float> Directions = { 0.f, 90.f, 180.f, 270.f };
    float BaseAngle = Directions[FMath::RandRange(0, 3)];
    float RandomAngle = BaseAngle + FMath::RandRange(-30.f, 30.f);
    float RandomRadius = FMath::RandRange(50.f, WanderRadius);

    FVector RandomOffset = FVector(
        FMath::Cos(FMath::DegreesToRadians(RandomAngle)) * RandomRadius,
        FMath::Sin(FMath::DegreesToRadians(RandomAngle)) * RandomRadius,
        0.f
    );

    FVector WanderTarget = AssignedPosition + RandomOffset;

    AIController->MoveToLocation(WanderTarget, 20.f, true, true, false, true, nullptr, false);

    float NextWander = FMath::RandRange(MinWanderInterval, MaxWanderInterval);
    GetWorld()->GetTimerManager().SetTimer(
        WanderTimer,
        this,
        &AEnemyCharacter::DoWander,
        NextWander,
        false
    );
}

void AEnemyCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsDead)
    {
        FacePlayer(DeltaTime);
        UpdateKnockback(DeltaTime);
        UpdateAttack(DeltaTime);
    }
}

void AEnemyCharacter::UpdateKnockback(float DeltaTime)
{
    if (!bKnockbackActive || HitKnockbackDuration <= 0.f) return;

    // Ease out - a fast shove that settles, frozen along with everything else during hit pause
    auto EaseOut = [](float X) { return 1.f - FMath::Square(1.f - X); };

    const float PreviousAlpha = KnockbackElapsed / HitKnockbackDuration;
    KnockbackElapsed = FMath::Min(KnockbackElapsed + DeltaTime, HitKnockbackDuration);
    const float Alpha = KnockbackElapsed / HitKnockbackDuration;

    const float Step = (EaseOut(Alpha) - EaseOut(PreviousAlpha)) * KnockbackDistance;
    AddActorWorldOffset(KnockbackDirection * Step, true);

    if (Alpha >= 1.f)
    {
        bKnockbackActive = false;
    }
}

void AEnemyCharacter::FacePlayer(float DeltaTime)
{
    // Dont fight animations during hit react, stagger or the paired counter
    if (bIsBeingHit || bIsStaggered || AttackState == EEnemyAttackState::Countered) return;

    if (!CachedPlayer.IsValid())
    {
        CachedPlayer = Cast<APlayerCharacter>(
            UGameplayStatics::GetPlayerCharacter(GetWorld(), 0)
        );
    }

    if (!CachedPlayer.IsValid()) return;

    FVector DirectionToPlayer = (CachedPlayer->GetActorLocation() -
        GetActorLocation()).GetSafeNormal();
    DirectionToPlayer.Z = 0.f;

    FRotator TargetRotation = DirectionToPlayer.Rotation();
    // Track the player harder while winding up a punch
    const bool bWindingUp = AttackState == EEnemyAttackState::Telegraph || AttackState == EEnemyAttackState::Striking;
    FRotator NewRotation = FMath::RInterpTo(
        GetActorRotation(),
        TargetRotation,
        DeltaTime,
        bWindingUp ? AttackTurnSpeed : RotationSpeed
    );

    SetActorRotation(NewRotation);
}

void AEnemyCharacter::TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator)
{
    if (bIsDead) return;

    // Already down - take the damage but let the stagger play out
    if (bIsStaggered)
    {
        LastHitTime = GetWorld()->GetTimeSeconds();
        Super::TakeCombatHit(Hit, DamageInstigator);
        return;
    }

    bIsBeingHit = false;
    bIsRecovering = false;
    GetWorld()->GetTimerManager().ClearTimer(HitReactRecoveryTimer);

    // Getting hit interrupts a wind-up (and the counter reaction, if one was playing)
    CancelAttack();
    LastHitTime = GetWorld()->GetTimeSeconds();

    Super::TakeCombatHit(Hit, DamageInstigator);

    if (bIsDead) return;

    bIsBeingHit = true;
    GetCharacterMovement()->StopMovementImmediately();

    AAIController* AIController = Cast<AAIController>(GetController());
    if (AIController)
    {
        AIController->StopMovement();
    }

    UAnimMontage* MontageToPlay = nullptr;
    FName SectionName = NAME_None;

    // Face the attacker so the reaction plays in the right direction - the snap is hidden by the hit react
    FVector ToAttacker = FVector::ZeroVector;
    if (DamageInstigator)
    {
        ToAttacker = DamageInstigator->GetActorLocation() - GetActorLocation();
        ToAttacker.Z = 0.f;
        if (!ToAttacker.IsNearlyZero())
        {
            SetActorRotation(ToAttacker.Rotation());
        }
    }

    // Empty list plays the whole montage
    auto PickSection = [](const TArray<FName>& Sections)
    {
        return Sections.IsEmpty() ? NAME_None : Sections[FMath::RandRange(0, Sections.Num() - 1)];
    };

    // The attack decides how likely it is to put us on the ground
    const bool bKnockedDown = Hit.KnockdownChance > 0.f && FMath::FRand() < Hit.KnockdownChance;

    if (bKnockedDown)
    {
        bIsStaggered = true;

        if (StaggerMontage_Large)
        {
            MontageToPlay = StaggerMontage_Large;
            SectionName = PickSection(StaggerSections);
        }
    }
    else if (HitReactMontage)
    {
        MontageToPlay = HitReactMontage;
        SectionName = PickSection(HitReactSections);
    }

    // Staggers carry their own root motion knockback
    if (!bIsStaggered && !ToAttacker.IsNearlyZero())
    {
        KnockbackDirection = -ToAttacker.GetSafeNormal();
        KnockbackDistance = HitKnockbackDistance * Hit.KnockbackScale;
        KnockbackElapsed = 0.f;
        bKnockbackActive = true;
    }

    if (MontageToPlay)
    {
        float SectionLength = 0.f;
        int32 SectionIndex = MontageToPlay->GetSectionIndex(SectionName);
        if (SectionIndex != INDEX_NONE)
        {
            SectionLength = MontageToPlay->GetSectionLength(SectionIndex);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Section '%s' not found in montage '%s' - using fallback"),
                *SectionName.ToString(), *MontageToPlay->GetName());
            SectionLength = MontageToPlay->GetPlayLength();
        }

        // Safety cap - never lock enemy longer than 5 seconds
        SectionLength = FMath::Min(SectionLength, 5.f);

        PlayAnimMontage(MontageToPlay, 1.f, SectionName);

        GetWorld()->GetTimerManager().SetTimer(
            HitReactRecoveryTimer,
            this,
            &AEnemyCharacter::OnHitReactEnd,
            SectionLength,
            false
        );
    }
    else
    {
        // No montage - clear hit state immediately
        bIsBeingHit = false;
        bIsStaggered = false;
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}

void AEnemyCharacter::OnHitReactEnd()
{
    bIsBeingHit = false;
    bIsStaggered = false;
    bIsRecovering = false;

    // StopMovementImmediately sets mode to None internally
    // Must explicitly restore Walking
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);

    AAIController* AIController = Cast<AAIController>(GetController());
    if (AIController && bHasAssignedPosition)
    {
        AIController->MoveToLocation(AssignedPosition, 20.f, true,
            true, false, true, nullptr, false);
    }

    GetWorld()->GetTimerManager().ClearTimer(WanderTimer);
    GetWorld()->GetTimerManager().SetTimer(
        WanderTimer,
        this,
        &AEnemyCharacter::DoWander,
        MinWanderInterval,
        false
    );
}

void AEnemyCharacter::Die()
{
    CancelAttack();
    Super::Die();

    HideOutline();

    GetWorld()->GetTimerManager().ClearTimer(HitReactRecoveryTimer);
    GetWorld()->GetTimerManager().ClearTimer(WanderTimer);

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCharacterMovement()->DisableMovement();
    GetCharacterMovement()->StopMovementImmediately();

    if (DeathMontage)
    {
        // Use section length to avoid full montage length bug
        float MontageLength = DeathMontage->GetPlayLength();
        PlayAnimMontage(DeathMontage);

        GetWorld()->GetTimerManager().SetTimer(
            DeathRemovalTimer,
            this,
            &AEnemyCharacter::RemoveBody,
            MontageLength,
            false
        );
    }
    else
    {
        Destroy();
    }
}

void AEnemyCharacter::RemoveBody()
{
    Destroy();
}

void AEnemyCharacter::ShowOutline()
{
    if (OutlineMaterial)
    {
        GetVisibleMesh()->SetOverlayMaterial(OutlineMaterial);
    }
}

void AEnemyCharacter::HideOutline()
{
    GetVisibleMesh()->SetOverlayMaterial(nullptr);
}

void AEnemyCharacter::OnDeathAnimationEnd()
{
    GetMesh()->bPauseAnims = true;
}

// ---------------------------------------------------------------------------
// Attacking
// ---------------------------------------------------------------------------

bool AEnemyCharacter::CanStartAttack() const
{
    const bool bRecentlyHit = GetWorld()->GetTimeSeconds() - LastHitTime < AttackLockoutAfterHit;
    return !bIsDead && !bIsBeingHit && !bIsStaggered && !bRecentlyHit &&
        AttackState == EEnemyAttackState::None && !Attacks.IsEmpty();
}

FStrikeInfo AEnemyCharacter::GetStrikeInfo(const UAttackData* Attack)
{
    if (const FStrikeInfo* Cached = AttackStrikeCache.Find(Attack))
    {
        return *Cached;
    }

    FStrikeInfo Info;
    float SectionStart = 0.f;
    float SectionEnd = 0.f;
    if (StrikeAnalysis::GetSectionRange(Attack->Montage, Attack->Section, SectionStart, SectionEnd))
    {
        const FVector Forward = GetMesh()->GetRelativeRotation().UnrotateVector(FVector::ForwardVector);
        Info = StrikeAnalysis::Analyze(Attack->Montage, SectionStart, SectionEnd,
            { TEXT("hand_r"), TEXT("hand_l"), TEXT("foot_r"), TEXT("foot_l") }, Forward, ContactExtension);
    }

    AttackStrikeCache.Add(Attack, Info);
    UE_LOG(LogTemp, Log, TEXT("Enemy strike %s: %s at %.2fs, reach %.0f%s"), *Attack->GetName(),
        *Info.Bone.ToString(), Info.ContactTime, Info.Reach, Info.bValid ? TEXT("") : TEXT(" (not detected, using fallback)"));
    return Info;
}

void AEnemyCharacter::StartAttack(AActor* Target)
{
    if (!Target || !CanStartAttack()) return;

    CurrentAttack = Attacks[FMath::RandRange(0, Attacks.Num() - 1)];
    if (!CurrentAttack || !CurrentAttack->Montage) return;

    float SectionStart = 0.f;
    float SectionEnd = CurrentAttack->Montage->GetPlayLength();
    StrikeAnalysis::GetSectionRange(CurrentAttack->Montage, CurrentAttack->Section, SectionStart, SectionEnd);
    const float SectionLength = SectionEnd - SectionStart;

    const FStrikeInfo Strike = GetStrikeInfo(CurrentAttack);
    const float ContactTime = Strike.bValid ? Strike.ContactTime : SectionLength * 0.4f;
    const float Reach = Strike.bValid ? Strike.Reach : CurrentAttack->FallbackStrikeDistance;
    AttackStrikeBone = Strike.bValid ? Strike.Bone : FName(TEXT("hand_r"));

    // Slow the swing so it fills the counter window, then spend whatever is left closing in
    AttackPlayRate = FMath::Clamp(ContactTime / FMath::Max(CounterWindow, KINDA_SMALL_NUMBER), MinAttackPlayRate, 1.f);
    ContactDelay = ContactTime / AttackPlayRate;
    TelegraphDelay = FMath::Max(CounterWindow - ContactDelay, 0.f);
    AttackEndTime = TelegraphDelay + SectionLength / AttackPlayRate;
    AttackStrikeDistance = Reach + Target->GetSimpleCollisionRadius() - AttackStrikePenetration;

    AttackTarget = Target;
    AttackElapsed = 0.f;
    AttackState = EEnemyAttackState::Telegraph;
    bIsTelegraphing = true;
    SetIndicatorVisible(true);

    if (TelegraphDelay <= 0.f)
    {
        BeginStrike();
        return;
    }

    // Walk in during the telegraph so the locomotion animation carries the approach
    if (AAIController* AIController = Cast<AAIController>(GetController()))
    {
        AIController->MoveToActor(Target, AttackStrikeDistance * 0.5f);
    }
}

void AEnemyCharacter::BeginStrike()
{
    if (AAIController* AIController = Cast<AAIController>(GetController()))
    {
        AIController->StopMovement();
    }

    // The code-driven approach below replaces the swing's own root motion
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();

    AttackState = EEnemyAttackState::Striking;

    PlayAnimMontage(CurrentAttack->Montage, AttackPlayRate, CurrentAttack->Section);
}

void AEnemyCharacter::UpdateAttack(float DeltaTime)
{
    if (AttackState == EEnemyAttackState::None) return;

    AttackElapsed += DeltaTime;
    const float ContactTime = TelegraphDelay + ContactDelay;

    switch (AttackState)
    {
    case EEnemyAttackState::Telegraph:
        if (AttackElapsed >= TelegraphDelay)
        {
            BeginStrike();
        }
        break;

    case EEnemyAttackState::Striking:
    {
        // Home in so the fist arrives at the player on the contact frame - if they move away, it whiffs
        if (const AActor* Target = AttackTarget.Get(); Target && DeltaTime > 0.f)
        {
            FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
            ToTarget.Z = 0.f;
            const float Distance = ToTarget.Size();

            if (Distance > KINDA_SMALL_NUMBER)
            {
                const float TimeLeft = FMath::Max(ContactTime - AttackElapsed, DeltaTime);
                const float MaxStep = AttackMaxApproachSpeed * DeltaTime;
                const float Step = FMath::Clamp((Distance - AttackStrikeDistance) * (DeltaTime / TimeLeft), -MaxStep, MaxStep);
                AddActorWorldOffset(ToTarget / Distance * Step, true);
            }
        }

        if (AttackElapsed >= ContactTime)
        {
            ResolveAttackContact();
        }
        break;
    }

    case EEnemyAttackState::Recovering:
    case EEnemyAttackState::Countered:
        if (AttackElapsed >= AttackEndTime)
        {
            EndAttack();
        }
        break;

    default:
        break;
    }

    UpdateIndicator();
}

void AEnemyCharacter::ResolveAttackContact()
{
    AttackState = EEnemyAttackState::Recovering;
    bIsTelegraphing = false;
    SetIndicatorVisible(false);

    AArkhamCharacterBase* Victim = Cast<AArkhamCharacterBase>(AttackTarget.Get());
    if (!Victim || Victim->IsDead() || !CurrentAttack) return;

    FVector ToVictim = Victim->GetActorLocation() - GetActorLocation();
    ToVictim.Z = 0.f;

    const bool bInReach = ToVictim.Size() <= AttackStrikeDistance + AttackReachTolerance;
    const bool bFacing = FVector::DotProduct(GetActorForwardVector(), ToVictim.GetSafeNormal()) > 0.5f;
    if (bInReach && bFacing)
    {
        Victim->TakeCombatHit(CurrentAttack->Hit, this);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("%s punch missed: distance %.0f (reach %.0f), facing %.2f (needs 0.5)"), *GetName(),
            ToVictim.Size(), AttackStrikeDistance + AttackReachTolerance,
            FVector::DotProduct(GetActorForwardVector(), ToVictim.GetSafeNormal()));
    }
}

void AEnemyCharacter::EndAttack()
{
    const bool bWasCountered = AttackState == EEnemyAttackState::Countered;

    AttackState = EEnemyAttackState::None;
    AttackTarget.Reset();
    bIsTelegraphing = false;
    SetIndicatorVisible(false);
    RestoreMovement();

    if (bWasCountered)
    {
        GetWorld()->GetTimerManager().SetTimer(WanderTimer, this, &AEnemyCharacter::DoWander, MinWanderInterval, false);
    }
}

void AEnemyCharacter::CancelAttack()
{
    if (AttackState == EEnemyAttackState::None) return;

    if (AttackState == EEnemyAttackState::Telegraph)
    {
        if (AAIController* AIController = Cast<AAIController>(GetController()))
        {
            AIController->StopMovement();
        }
    }

    AttackState = EEnemyAttackState::None;
    AttackTarget.Reset();
    bIsTelegraphing = false;
    SetIndicatorVisible(false);
    RestoreMovement();
}

void AEnemyCharacter::RestoreMovement()
{
    if (!bIsDead && GetCharacterMovement()->MovementMode == MOVE_None)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}

bool AEnemyCharacter::IsCounterable() const
{
    return AttackState == EEnemyAttackState::Telegraph || AttackState == EEnemyAttackState::Striking;
}

float AEnemyCharacter::GetTimeToContact() const
{
    return IsCounterable() ? TelegraphDelay + ContactDelay - AttackElapsed : TNumericLimits<float>::Max();
}

// ---------------------------------------------------------------------------
// Countered
// ---------------------------------------------------------------------------

void AEnemyCharacter::PlayCountered(AActor* Counterer)
{
    if (bIsDead) return;

    if (AAIController* AIController = Cast<AAIController>(GetController()))
    {
        AIController->StopMovement();
    }

    SetIndicatorVisible(false);
    bIsTelegraphing = false;
    bKnockbackActive = false;

    // The paired animation carries both characters with root motion, so walking must be on
    AttackState = EEnemyAttackState::Countered;
    AttackElapsed = 0.f;
    RestoreMovement();
    GetCharacterMovement()->StopMovementImmediately();

    // Facing is set by whoever lines up the pair (UCombatComponent::StartCounter) - turning toward the counterer here
    // would undo its side offset

    AttackEndTime = CounteredMontage ? CounteredMontage->GetPlayLength() : 0.5f;
    if (CounteredMontage)
    {
        PlayAnimMontage(CounteredMontage);
    }
}

void AEnemyCharacter::TakeCounterHit(const FCombatHit& Hit, AActor* DamageInstigator)
{
    // Damage only - the paired animation is already the reaction
    AArkhamCharacterBase::TakeCombatHit(Hit, DamageInstigator);
}

// ---------------------------------------------------------------------------
// Indicator
// ---------------------------------------------------------------------------

void AEnemyCharacter::SetIndicatorVisible(bool bVisible)
{
    if (!AttackIndicatorRing) return;

    AttackIndicatorRing->SetHiddenInGame(!bVisible);
    if (bVisible)
    {
        // Place it before the first rendered frame
        UpdateIndicator();
    }
}

void AEnemyCharacter::UpdateIndicator()
{
    if (!AttackIndicatorRing || AttackIndicatorRing->bHiddenInGame) return;

    // Follow the head (or the punching hand) and face the camera
    const FName Bone = bIndicatorOnStrikingHand && !AttackStrikeBone.IsNone() ? AttackStrikeBone : IndicatorHeadBone;
    const FVector Location = GetMesh()->GetSocketLocation(Bone) + IndicatorOffset;

    FRotator Rotation = AttackIndicatorRing->GetComponentRotation();
    if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        // The plane's face is +Z
        Rotation = FRotationMatrix::MakeFromZ(CameraManager->GetCameraLocation() - Location).Rotator();
    }

    const float Size = (bIndicatorOnStrikingHand ? IndicatorHandSize : IndicatorHeadSize) / 100.f;
    AttackIndicatorRing->SetWorldLocationAndRotation(Location, Rotation);
    AttackIndicatorRing->SetWorldScale3D(FVector(Size));

    // How close the punch is - the material turns this into colour, flashing and pulsing
    if (IndicatorMID)
    {
        const float Urgency = FMath::Clamp(AttackElapsed / FMath::Max(TelegraphDelay + ContactDelay, KINDA_SMALL_NUMBER), 0.f, 1.f);
        IndicatorMID->SetScalarParameterValue(IndicatorUrgencyParameter, Urgency);
    }
}