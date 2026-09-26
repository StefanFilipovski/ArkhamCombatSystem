#include "Combat/AnimNotifyState_HitWindow.h"
#include "Combat/CombatComponent.h"
#include "Animation/ActiveMontageInstanceScope.h"

namespace
{
    UCombatComponent* GetCombatComponent(const USkeletalMeshComponent* MeshComp)
    {
        return (MeshComp && MeshComp->GetOwner())
            ? MeshComp->GetOwner()->FindComponentByClass<UCombatComponent>()
            : nullptr;
    }

    int32 GetMontageInstanceID(const FAnimNotifyEventReference& EventReference)
    {
        const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
            EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
        return MontageContext ? MontageContext->MontageInstanceID : INDEX_NONE;
    }
}

void UAnimNotifyState_HitWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

    if (UCombatComponent* CombatComp = GetCombatComponent(MeshComp))
    {
        CombatComp->EnableHitbox(Animation, GetMontageInstanceID(EventReference));
    }
}

void UAnimNotifyState_HitWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyEnd(MeshComp, Animation, EventReference);

    if (UCombatComponent* CombatComp = GetCombatComponent(MeshComp))
    {
        CombatComp->DisableHitbox(Animation, GetMontageInstanceID(EventReference));
    }
}
