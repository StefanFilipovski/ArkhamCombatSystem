#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_HitWindow.generated.h"

// Alternative to the EnableHitbox/DisableHitbox pair: one bar spanning the active frames.
// NotifyEnd is guaranteed even when the montage is interrupted, so the window can't get stuck open.
UCLASS(meta = (DisplayName = "Hit Window"))
class ARKHAMCOMBAT_API UAnimNotifyState_HitWindow : public UAnimNotifyState
{
    GENERATED_BODY()

public:
    virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

    virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;
};
