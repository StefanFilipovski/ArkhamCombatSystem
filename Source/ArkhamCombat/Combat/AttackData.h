#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AttackData.generated.h"

class UAnimMontage;

// What a hit does to whoever it lands on. Carried from the attacker to the victim.
USTRUCT(BlueprintType)
struct FCombatHit
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit")
    float Damage = 25.f;

    // 0 = always a light flinch, 1 = always knocked down
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float KnockdownChance = 0.f;

    // Multiplies how far a light hit pushes the victim back
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit", meta = (ClampMin = "0.0"))
    float KnockbackScale = 1.f;

    // Freeze frame on impact, in seconds
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel", meta = (ClampMin = "0.0"))
    float HitPauseDuration = 0.05f;

    // Strength of the hit camera shake
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel", meta = (ClampMin = "0.0"))
    float CameraShakeScale = 0.4f;
};

/**
 * One attack: which animation plays and how hard it hits.
 * Create in the editor (Content Browser > Miscellaneous > Data Asset > AttackData) and add to a
 * character's attack lists. Contact timing and the striking limb are read from the animation.
 */
UCLASS(BlueprintType)
class ARKHAMCOMBAT_API UAttackData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
    TObjectPtr<UAnimMontage> Montage;

    // Montage section to play. None plays the whole montage.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
    FName Section;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit", meta = (ShowOnlyInnerProperties))
    FCombatHit Hit;

    // Only the enemy the attack was aimed at can be hit (stops knockdown kicks clipping bystanders)
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit")
    bool bOnlyHitsTarget = false;

    // Bone/socket swept for hits. None uses the striking limb read from the animation.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit")
    FName HitSocketOverride;

    // Where to stop in front of the target if the reach can't be read from the animation
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
    float FallbackStrikeDistance = 90.f;
};
