#include "Combat/AnimNotify_CounterImpact.h"
#include "Combat/CombatComponent.h"

void UAnimNotify_CounterImpact::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp || !MeshComp->GetOwner()) return;

    if (UCombatComponent* CombatComp = MeshComp->GetOwner()->FindComponentByClass<UCombatComponent>())
    {
        CombatComp->OnCounterImpact(Animation);
    }
}
