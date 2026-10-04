#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/AttackData.h"
#include "ArkhamCharacterBase.generated.h"

class AArkhamCharacterBase;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, AArkhamCharacterBase*, Character);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, CurrentHealth, float, MaxHealth);

// One set of materials for the visible model, in material slot order
USTRUCT(BlueprintType)
struct FCharacterLook
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    TArray<TObjectPtr<UMaterialInterface>> Materials;
};

UCLASS()
class ARKHAMCOMBAT_API AArkhamCharacterBase : public ACharacter
{
    GENERATED_BODY()

public:
    AArkhamCharacterBase();

    virtual void OnConstruction(const FTransform& Transform) override;

   
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Appearance")
    TObjectPtr<USkeletalMeshComponent> VisualMesh;

    UFUNCTION(BlueprintPure, Category = "Appearance")
    USkeletalMeshComponent* GetVisibleMesh() const;

protected:
    virtual void BeginPlay() override;

    // One of these is picked at random on spawn. Empty = keep the materials set on the mesh.
    UPROPERTY(EditDefaultsOnly, Category = "Appearance")
    TArray<FCharacterLook> Looks;

    // --- Health ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
    float MaxHealth = 200.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    float CurrentHealth;

    // --- State ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bIsDead = false;

public:
    virtual void Tick(float DeltaTime) override;

    // Applies an incoming hit. What the hit does beyond damage (knockdown, knockback, feel) is up to the subclass.
    UFUNCTION(BlueprintCallable, Category = "Combat")
    virtual void TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator);

    UFUNCTION(BlueprintCallable, Category = "Combat")
    virtual void Die();

    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FOnCharacterDied OnDied;

    // Fires whenever health changes - UI listens here
    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FOnHealthChanged OnHealthChanged;

    // --- Getters ---
    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsDead() const { return bIsDead; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    float GetCurrentHealth() const { return CurrentHealth; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    float GetHealthPercent() const { return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f; }

    // Drives the fists-up stance in the anim blueprint
    UFUNCTION(BlueprintPure, Category = "Combat")
    virtual bool IsInCombat() const { return false; }
};