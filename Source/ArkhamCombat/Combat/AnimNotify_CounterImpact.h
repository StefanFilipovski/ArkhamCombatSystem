#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_CounterImpact.generated.h"

// Place on the frame the counter strike connects. The combat component applies the counter damage when it fires.
UCLASS(meta = (DisplayName = "Counter Impact"))
class ARKHAMCOMBAT_API UAnimNotify_CounterImpact : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp,
        UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;
};
