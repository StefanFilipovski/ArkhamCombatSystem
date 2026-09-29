#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/AttackData.h"
#include "ArkhamCharacterBase.generated.h"

class AArkhamCharacterBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, AArkhamCharacterBase*, Character);

UCLASS()
class ARKHAMCOMBAT_API AArkhamCharacterBase : public ACharacter
{
    GENERATED_BODY()

public:
    AArkhamCharacterBase();

protected:
    virtual void BeginPlay() override;

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

    // --- Getters ---
    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsDead() const { return bIsDead; }

    UFUNCTION(BlueprintPure, Category = "Combat")
    float GetCurrentHealth() const { return CurrentHealth; }
};