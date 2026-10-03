#include "Characters/ArkhamCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"

AArkhamCharacterBase::AArkhamCharacterBase()
{
    PrimaryActorTick.bCanEverTick = true;

    VisualMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VisualMesh"));
    VisualMesh->SetupAttachment(GetMesh());
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualMesh->SetGenerateOverlapEvents(false);
}

void AArkhamCharacterBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Swap which mesh is drawn - also in the Blueprint viewport, so the editor shows what the game will
    const bool bHasVisualMesh = VisualMesh->GetSkeletalMeshAsset() != nullptr;
    GetMesh()->SetVisibility(!bHasVisualMesh, false);
    VisualMesh->SetVisibility(bHasVisualMesh, false);

    // Hidden meshes skip animation by default, but the combat code and the visible model both need its pose
    if (bHasVisualMesh)
    {
        GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    }
}

void AArkhamCharacterBase::BeginPlay()
{
    Super::BeginPlay();
    CurrentHealth = MaxHealth;

    // Copy the pose only after it has been updated this frame
    VisualMesh->AddTickPrerequisiteComponent(GetMesh());

    if (!Looks.IsEmpty())
    {
        const FCharacterLook& Look = Looks[FMath::RandRange(0, Looks.Num() - 1)];
        USkeletalMeshComponent* Visible = GetVisibleMesh();
        for (int32 Slot = 0; Slot < Look.Materials.Num(); ++Slot)
        {
            if (Look.Materials[Slot])
            {
                Visible->SetMaterial(Slot, Look.Materials[Slot]);
            }
        }
    }
}

USkeletalMeshComponent* AArkhamCharacterBase::GetVisibleMesh() const
{
    return VisualMesh->GetSkeletalMeshAsset() ? VisualMesh.Get() : GetMesh();
}

void AArkhamCharacterBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AArkhamCharacterBase::TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator)
{
    if (bIsDead) return;

    CurrentHealth = FMath::Clamp(CurrentHealth - Hit.Damage, 0.f, MaxHealth);
    OnHealthChanged.Broadcast(CurrentHealth, MaxHealth);

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

    OnDied.Broadcast(this);
}