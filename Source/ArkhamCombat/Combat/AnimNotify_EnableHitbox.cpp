#include "Combat/AnimNotify_EnableHitbox.h"
#include "Combat/CombatComponent.h"
#include "Animation/ActiveMontageInstanceScope.h"

void UAnimNotify_EnableHitbox::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp || !MeshComp->GetOwner()) return;

    UCombatComponent* CombatComp = MeshComp->GetOwner()->FindComponentByClass<UCombatComponent>();
    if (!CombatComp) return;

    // Identifies which montage instance fired this, so a blending-out attack can't open the next one's window
    const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
        EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();

    CombatComp->EnableHitbox(Animation, MontageContext ? MontageContext->MontageInstanceID : INDEX_NONE);
}
