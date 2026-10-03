#include "Combat/MontageSectionLibrary.h"
#include "Animation/AnimMontage.h"

bool UMontageSectionLibrary::SetMontageSections(UAnimMontage* Montage, const TArray<FName>& SectionNames, const TArray<float>& StartTimes)
{
#if WITH_EDITOR
    if (!Montage || SectionNames.IsEmpty() || SectionNames.Num() != StartTimes.Num())
    {
        return false;
    }

    Montage->Modify();

    // Built the way the montage editor builds them: each section linked to the montage timeline, so they can be dragged afterwards
    Montage->CompositeSections.Reset();
    for (int32 i = 0; i < SectionNames.Num(); ++i)
    {
        FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
        Section.SectionName = SectionNames[i];
        Section.Link(Montage, StartTimes[i]);
        Section.NextSectionName = NAME_None;
    }
    Montage->CompositeSections.Sort([](const FCompositeSection& A, const FCompositeSection& B) { return A.GetTime() < B.GetTime(); });

    Montage->PostEditChange();
    Montage->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
