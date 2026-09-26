#include "Characters/ArkhamCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AArkhamCharacterBase::AArkhamCharacterBase()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AArkhamCharacterBase::BeginPlay()
{
    Super::BeginPlay();
    CurrentHealth = MaxHealth;
}

void AArkhamCharacterBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AArkhamCharacterBase::TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator)
{
    if (bIsDead) return;

    CurrentHealth = FMath::Clamp(CurrentHealth - Hit.Damage, 0.f, MaxHealth);

    UE_LOG(LogTemp, Warning, TEXT("%s took %.1f damage. Health: %.1f"),
        *GetName(), Hit.Damage, CurrentHealth);

    if (CurrentHealth <= 0.f)
    {
        Die();
    }
}

void AArkhamCharacterBase::Die()
{
    if (bIsDead) return;

    bIsDead = true;

    UE_LOG(LogTemp, Warning, TEXT("%s has died."), *GetName());

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCharacterMovement()->DisableMovement();
}