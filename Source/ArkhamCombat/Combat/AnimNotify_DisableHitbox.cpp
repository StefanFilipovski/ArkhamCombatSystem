#include "Combat/AnimNotify_DisableHitbox.h"
#include "Combat/CombatComponent.h"
#include "Animation/ActiveMontageInstanceScope.h"

void UAnimNotify_DisableHitbox::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp || !MeshComp->GetOwner()) return;

    UCombatComponent* CombatComp = MeshComp->GetOwner()->FindComponentByClass<UCombatComponent>();
    if (!CombatComp) return;

    const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
        EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();

    CombatComp->DisableHitbox(Animation, MontageContext ? MontageContext->MontageInstanceID : INDEX_NONE);
}
