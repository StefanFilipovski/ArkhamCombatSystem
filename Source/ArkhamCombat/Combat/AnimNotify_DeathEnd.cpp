#include "Combat/AnimNotify_DeathEnd.h"
#include "Characters/EnemyCharacter.h"

void UAnimNotify_DeathEnd::Notify(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp) return;

    AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(MeshComp->GetOwner());
    if (Enemy)
    {
        Enemy->OnDeathAnimationEnd();
    }
}