#include "AI/EnemyManagerComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/PlayerCharacter.h"
#include "Combat/CombatComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "AIController.h"

UEnemyManagerComponent::UEnemyManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UEnemyManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    Player = Cast<APlayerCharacter>(
        UGameplayStatics::GetPlayerCharacter(GetWorld(), 0)
    );
}

void UEnemyManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Pawn may not exist yet when the game mode begins play
    if (!Player)
    {
        Player = Cast<APlayerCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
    }

    TimeSinceLastUpdate += DeltaTime;
    if (TimeSinceLastUpdate >= UpdateInterval)
    {
        TimeSinceLastUpdate = 0.f;
        FindAllEnemies();
        UpdateEnemyPositions();
    }

    UpdateAttacks(DeltaTime);
}

void UEnemyManagerComponent::UpdateAttacks(float DeltaTime)
{
    if (!bEnemiesAttack || !Player || Player->IsDead()) return;

    const UCombatComponent* PlayerCombat = Player->FindComponentByClass<UCombatComponent>();
    const FVector PlayerLocation = Player->GetActorLocation();

    // Engaged: every living enemy in the fight. Candidates: the ones free to swing right now.
    int32 EngagedEnemies = 0;
    int32 CurrentAttackers = 0;
    TArray<AEnemyCharacter*> Candidates;

    for (AEnemyCharacter* Enemy : ActiveEnemies)
    {
        if (!IsValid(Enemy) || Enemy->IsDead()) continue;
        if (FVector::Dist2D(Enemy->GetActorLocation(), PlayerLocation) > AttackEngageRange) continue;

        ++EngagedEnemies;

        if (Enemy->IsAttacking())
        {
            ++CurrentAttackers;
        }
        // Whoever the player is busy beating on doesn't get to swing back - someone else does
        else if (Enemy->CanStartAttack() && !(PlayerCombat && PlayerCombat->IsEngaging(Enemy)))
        {
            Candidates.Add(Enemy);
        }
    }

    // Clock only runs once a fight is on
    if (EngagedEnemies == 0)
    {
        bAttackTimerStarted = false;
        return;
    }
    if (!bAttackTimerStarted)
    {
        bAttackTimerStarted = true;
        TimeUntilNextAttack = InitialAttackDelay;
    }

    // Bigger fights allow more attackers at once
    const int32 AllowedAttackers = FMath::Clamp(
        FMath::DivideAndRoundUp(EngagedEnemies, FMath::Max(1, EnemiesPerAttacker)), 1, FMath::Max(1, MaxSimultaneousAttackers));

    // Hold the clock while the attacker slots are full, so attacks don't queue up back to back
    if (CurrentAttackers >= AllowedAttackers) return;

    TimeUntilNextAttack -= DeltaTime;
    if (TimeUntilNextAttack > 0.f || Candidates.IsEmpty()) return;

    Candidates[FMath::RandRange(0, Candidates.Num() - 1)]->StartAttack(Player);

    // Lone enemies are patient, crowds keep the pressure on
    const float CrowdAlpha = FMath::Clamp(float(EngagedEnemies - 1) / float(FMath::Max(1, CrowdSize - 1)), 0.f, 1.f);
    const FVector2D Interval = FMath::Lerp(SoloAttackInterval, CrowdAttackInterval, CrowdAlpha);
    TimeUntilNextAttack = FMath::RandRange(Interval.X, Interval.Y);
}

void UEnemyManagerComponent::FindAllEnemies()
{
    ActiveEnemies.Empty();

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AEnemyCharacter::StaticClass(),
        FoundActors
    );

    for (AActor* Actor : FoundActors)
    {
        AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
        if (Enemy && !Enemy->IsDead())
        {
            ActiveEnemies.Add(Enemy);
        }
    }
}

TArray<FVector> UEnemyManagerComponent::CalculateSurroundPositions(int32 EnemyCount)
{
    TArray<FVector> Positions;
    if (!Player) return Positions;

    FVector PlayerLocation = Player->GetActorLocation();
    EEnemyGroupBehavior Behavior = GetGroupBehavior();

    // Generate all possible positions around player
    TArray<FVector> AvailablePositions;
    float AngleStep = 360.f / EnemyCount;

    for (int32 i = 0; i < EnemyCount; i++)
    {
        float Angle = FMath::DegreesToRadians(i * AngleStep);
        FVector Offset = FVector(
            FMath::Cos(Angle) * SurroundRadius,
            FMath::Sin(Angle) * SurroundRadius,
            0.f
        );
        AvailablePositions.Add(PlayerLocation + Offset);
    }

    // Assign each enemy to their NEAREST available position
    TArray<bool> PositionTaken;
    PositionTaken.Init(false, AvailablePositions.Num());

    for (AEnemyCharacter* Enemy : ActiveEnemies)
    {
        if (!Enemy || !IsValid(Enemy)) continue;

        float NearestDistance = FLT_MAX;
        int32 NearestIndex = 0;

        // Find closest available position to this enemy
        for (int32 i = 0; i < AvailablePositions.Num(); i++)
        {
            if (PositionTaken[i]) continue;

            float Distance = FVector::Dist(
                Enemy->GetActorLocation(),
                AvailablePositions[i]
            );

            if (Distance < NearestDistance)
            {
                NearestDistance = Distance;
                NearestIndex = i;
            }
        }

        PositionTaken[NearestIndex] = true;
        Positions.Add(AvailablePositions[NearestIndex]);
    }

    return Positions;
}

void UEnemyManagerComponent::UpdateEnemyPositions()
{
    if (!Player || ActiveEnemies.Num() == 0) return;

    TArray<FVector> SurroundPositions =
        CalculateSurroundPositions(ActiveEnemies.Num());

    for (int32 i = 0; i < ActiveEnemies.Num(); i++)
    {
        AEnemyCharacter* Enemy = ActiveEnemies[i];
        if (!Enemy || !IsValid(Enemy)) continue;

        // Skip enemies that are reacting or recovering
        if (Enemy->IsBeingHit() || Enemy->IsRecovering() || Enemy->IsStaggered() || Enemy->IsAttacking()) continue;

        Enemy->SetAssignedPosition(SurroundPositions[i]);

        AAIController* AIController = Cast<AAIController>(Enemy->GetController());
        if (!AIController) continue;

        FVector SeparationForce = CalculateSeparationForce(Enemy);
        FVector TargetPosition = SurroundPositions[i] + SeparationForce;

        float DistanceToTarget = FVector::Dist2D(
            Enemy->GetActorLocation(),
            TargetPosition
        );

        // Inside the wander area the enemy's own wander drives it - moving it here too made them jitter
        if (DistanceToTarget > Enemy->GetWanderRadius() + SlotReturnSlack)
        {
            AIController->MoveToLocation(
                TargetPosition,
                50.f,
                true,
                true,
                false,
                true,
                nullptr,
                false
            );
        }
    }
}

FVector UEnemyManagerComponent::CalculateSeparationForce(AEnemyCharacter* Enemy)
{
    FVector SeparationForce = FVector::ZeroVector;
    float SeparationRadius = 80.f;

    for (AEnemyCharacter* OtherEnemy : ActiveEnemies)
    {
        if (!OtherEnemy || OtherEnemy == Enemy) continue;

        FVector ToOther = OtherEnemy->GetActorLocation() -
            Enemy->GetActorLocation();
        float Distance = ToOther.Size();

        if (Distance < SeparationRadius && Distance > 0.f)
        {
            // Push away from other enemy scaled by how close they are
            float Strength = 1.f - (Distance / SeparationRadius);
            SeparationForce -= ToOther.GetSafeNormal() * Strength * 100.f;
        }
    }

    return SeparationForce;
}

EEnemyGroupBehavior UEnemyManagerComponent::GetGroupBehavior() const
{
    int32 Count = ActiveEnemies.Num();
    if (Count <= 1) return EEnemyGroupBehavior::Solo;
    if (Count == 2) return EEnemyGroupBehavior::Pair;
    if (Count <= 4) return EEnemyGroupBehavior::Group;
    return EEnemyGroupBehavior::Mob;
}