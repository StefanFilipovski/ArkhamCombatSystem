#include "AI/EnemySpawner.h"
#include "Characters/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

AEnemySpawner::AEnemySpawner()
{
    PrimaryActorTick.bCanEverTick = false;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AEnemySpawner::BeginPlay()
{
    Super::BeginPlay();

    if (!EnemyClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s has no EnemyClass set - nothing will spawn"), *GetName());
        bComplete = true;
        return;
    }

    // Links to actors in another level are dropped on copy-paste, leaving empty entries behind
    const int32 EmptyPoints = SpawnPoints.RemoveAll([](const TObjectPtr<AActor>& Point) { return !IsValid(Point); });
    if (EmptyPoints > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s has %d empty spawn points - reassign them in the level%s"), *GetName(), EmptyPoints,
            SpawnPoints.IsEmpty() ? TEXT(". Using random nav mesh spots for now.") : TEXT(""));
    }

    // The player pawn may not be spawned yet, and spawn points are picked relative to it
    GetWorldTimerManager().SetTimerForNextTick(this, &AEnemySpawner::SpawnInitialEnemies);
}

int32 AEnemySpawner::GetDesiredAliveCount() const
{
    // The crowd grows as the player gets through it
    const int32 Extra = DefeatsPerExtraEnemy > 0 ? DefeatedCount / DefeatsPerExtraEnemy : 0;
    return FMath::Min(InitialEnemies + Extra, MaxAliveEnemies);
}

void AEnemySpawner::SpawnInitialEnemies()
{
    const int32 Count = FMath::Min(GetDesiredAliveCount(), TotalEnemies);
    for (int32 i = 0; i < Count; ++i)
    {
        TrySpawnEnemy();
    }

    // Anyone who couldn't be placed tries again shortly
    QueueSpawns();
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

void AEnemySpawner::QueueSpawns()
{
    if (bComplete) return;

    const int32 StillToSpawn = TotalEnemies - SpawnedCount - PendingSpawns;
    const int32 Missing = GetDesiredAliveCount() - LivingEnemies.Num() - PendingSpawns;
    const int32 ToQueue = FMath::Min(StillToSpawn, Missing);
    if (ToQueue <= 0) return;

    const bool bTimerIdle = PendingSpawns == 0;
    PendingSpawns += ToQueue;

    if (bTimerIdle)
    {
        GetWorldTimerManager().SetTimer(SpawnTimer, this, &AEnemySpawner::SpawnQueuedEnemy,
            FMath::FRandRange(RespawnDelay.X, RespawnDelay.Y), false);
    }
}

void AEnemySpawner::SpawnQueuedEnemy()
{
    if (bComplete || PendingSpawns <= 0) return;

    if (!TrySpawnEnemy())
    {
        // Every spot is blocked right now - try again soon
        GetWorldTimerManager().SetTimer(SpawnTimer, this, &AEnemySpawner::SpawnQueuedEnemy, 0.5f, false);
        return;
    }

    --PendingSpawns;

    // Several queued at once arrive one after another rather than all in the same frame
    if (PendingSpawns > 0)
    {
        GetWorldTimerManager().SetTimer(SpawnTimer, this, &AEnemySpawner::SpawnQueuedEnemy,
            FMath::FRandRange(0.3f, 0.8f), false);
    }
}

AEnemyCharacter* AEnemySpawner::TrySpawnEnemy()
{
    FVector Location;
    if (!PickSpawnLocation(Location)) return nullptr;

    // Stand on the floor, facing the player
    const AEnemyCharacter* Defaults = EnemyClass->GetDefaultObject<AEnemyCharacter>();
    Location.Z += Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

    FRotator Facing = GetActorRotation();
    if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
    {
        Facing = FRotator(0.f, (Player->GetActorLocation() - Location).Rotation().Yaw, 0.f);
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

    AEnemyCharacter* Enemy = GetWorld()->SpawnActor<AEnemyCharacter>(EnemyClass, Location, Facing, Params);
    if (!Enemy) return nullptr;

    if (!Enemy->GetController())
    {
        Enemy->SpawnDefaultController();
    }

    Enemy->OnDied.AddDynamic(this, &AEnemySpawner::HandleEnemyDied);
    Enemy->OnDestroyed.AddDynamic(this, &AEnemySpawner::HandleEnemyDestroyed);

    LivingEnemies.Add(Enemy);
    ++SpawnedCount;

    UE_LOG(LogTemp, Log, TEXT("Spawner: spawned %s (%d/%d)"), *Enemy->GetName(), SpawnedCount, TotalEnemies);
    return Enemy;
}

bool AEnemySpawner::PickSpawnLocation(FVector& OutLocation)
{
    return SpawnPoints.IsEmpty() ? PickNavigableLocation(OutLocation) : PickSpawnPoint(OutLocation);
}

bool AEnemySpawner::PickSpawnPoint(FVector& OutLocation)
{
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);

    TArray<AActor*> Free;       // nobody standing on it
    TArray<AActor*> Far;        // ...and not right next to the player
    TArray<AActor*> Offscreen;  // ...and out of the camera's view

    for (AActor* Point : SpawnPoints)
    {
        if (!IsValid(Point) || IsSpotTaken(Point->GetActorLocation())) continue;
        Free.Add(Point);

        const FVector Location = Point->GetActorLocation();
        if (Player && FVector::Dist2D(Location, Player->GetActorLocation()) < MinSpawnDistance) continue;
        Far.Add(Point);

        if (Camera)
        {
            // A little wider than the real FOV so points on the screen edge count as visible
            const FVector ToPoint = (Location - Camera->GetCameraLocation()).GetSafeNormal();
            const float VisibleCos = FMath::Cos(FMath::DegreesToRadians(Camera->GetFOVAngle() * 0.5f + 10.f));
            if (FVector::DotProduct(Camera->GetCameraRotation().Vector(), ToPoint) < VisibleCos)
            {
                Offscreen.Add(Point);
            }
        }
    }

    TArray<AActor*>* Pool = (bPreferOffscreenSpawns && !Offscreen.IsEmpty()) ? &Offscreen : &Far;

    // Player is camping next to every point - fall back to the furthest free one rather than stalling
    if (Pool->IsEmpty())
    {
        if (Free.IsEmpty()) return false;

        AActor* Furthest = Free[0];
        if (Player)
        {
            for (AActor* Point : Free)
            {
                if (FVector::DistSquared2D(Point->GetActorLocation(), Player->GetActorLocation()) >
                    FVector::DistSquared2D(Furthest->GetActorLocation(), Player->GetActorLocation()))
                {
                    Furthest = Point;
                }
            }
        }
        Pool = &Free;
        Pool->Reset();
        Pool->Add(Furthest);
    }

    // Don't use the same door twice in a row if there's a choice
    if (Pool->Num() > 1)
    {
        Pool->Remove(LastSpawnPoint.Get());
    }

    AActor* Chosen = (*Pool)[FMath::RandRange(0, Pool->Num() - 1)];
    LastSpawnPoint = Chosen;
    OutLocation = Chosen->GetActorLocation();

    // Snap onto the nav mesh so the enemy can walk in straight away
    if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        FNavLocation Projected;
        if (Nav->ProjectPointToNavigation(OutLocation, Projected, FVector(100.f, 100.f, 300.f)))
        {
            OutLocation = Projected.Location;
        }
    }
    return true;
}

bool AEnemySpawner::PickNavigableLocation(FVector& OutLocation) const
{
    const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return false;

    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);

    for (int32 Attempt = 0; Attempt < 10; ++Attempt)
    {
        FNavLocation Candidate;
        if (!Nav->GetRandomReachablePointInRadius(GetActorLocation(), FallbackSpawnRadius, Candidate)) continue;
        if (IsSpotTaken(Candidate.Location)) continue;
        if (Player && FVector::Dist2D(Candidate.Location, Player->GetActorLocation()) < MinSpawnDistance) continue;

        OutLocation = Candidate.Location;
        return true;
    }
    return false;
}

bool AEnemySpawner::IsSpotTaken(const FVector& Location) const
{
    for (const AEnemyCharacter* Enemy : LivingEnemies)
    {
        if (IsValid(Enemy) && FVector::Dist2D(Enemy->GetActorLocation(), Location) < 120.f) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Defeats
// ---------------------------------------------------------------------------

void AEnemySpawner::HandleEnemyDied(AArkhamCharacterBase* Character)
{
    RegisterDefeat(Cast<AEnemyCharacter>(Character));
}

void AEnemySpawner::HandleEnemyDestroyed(AActor* DestroyedActor)
{
    // Removed without dying (fell out of the world) - still counts, or the encounter could never finish
    RegisterDefeat(Cast<AEnemyCharacter>(DestroyedActor));
}

void AEnemySpawner::RegisterDefeat(AEnemyCharacter* Enemy)
{
    if (!Enemy || LivingEnemies.Remove(Enemy) == 0) return;

    ++DefeatedCount;
    UE_LOG(LogTemp, Log, TEXT("Spawner: %d/%d defeated"), DefeatedCount, TotalEnemies);
    OnEnemyDefeated.Broadcast(DefeatedCount, TotalEnemies);

    if (DefeatedCount >= TotalEnemies)
    {
        CompleteEncounter();
    }
    else
    {
        QueueSpawns();
    }
}

void AEnemySpawner::CompleteEncounter()
{
    if (bComplete) return;

    bComplete = true;
    PendingSpawns = 0;
    GetWorldTimerManager().ClearTimer(SpawnTimer);

    UE_LOG(LogTemp, Log, TEXT("Spawner: encounter complete"));
    OnEncounterComplete.Broadcast();

    if (RestartDelayAfterComplete > 0.f)
    {
        GetWorldTimerManager().SetTimer(RestartTimer, this, &AEnemySpawner::RestartLevel, RestartDelayAfterComplete, false);
    }
}

void AEnemySpawner::RestartLevel()
{
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}
