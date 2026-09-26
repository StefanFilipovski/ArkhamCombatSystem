#include "Combat/StrikeAnalysis.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

namespace
{
    // Resolves a time on the asset to the sequence and local time playing there
    const UAnimSequence* FindSequence(const UAnimSequenceBase* Animation, float Time, float& OutAnimTime)
    {
        if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Animation))
        {
            OutAnimTime = Time;
            return Sequence;
        }

        const UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
        if (!Montage || Montage->SlotAnimTracks.IsEmpty()) return nullptr;

        const FAnimSegment* Segment = Montage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(Time);
        if (!Segment) return nullptr;

        OutAnimTime = Segment->ConvertTrackPosToAnimPos(Time);
        return Cast<UAnimSequence>(Segment->GetAnimReference());
    }

    FTransform SampleBone(const UAnimSequence* Sequence, int32 BoneIndex, float AnimTime)
    {
        FTransform Local;
        Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(BoneIndex), FAnimExtractContext(static_cast<double>(AnimTime)), false);
        return Local;
    }
}

bool StrikeAnalysis::GetSectionRange(const UAnimMontage* Montage, FName Section, float& OutStart, float& OutEnd)
{
    if (!Montage) return false;

    if (Section.IsNone())
    {
        OutStart = 0.f;
        OutEnd = Montage->GetPlayLength();
        return true;
    }

    const int32 SectionIndex = Montage->GetSectionIndex(Section);
    if (SectionIndex == INDEX_NONE) return false;

    Montage->GetSectionStartAndEndTime(SectionIndex, OutStart, OutEnd);
    return true;
}

FStrikeInfo StrikeAnalysis::Analyze(const UAnimSequenceBase* Animation, float StartTime, float EndTime,
    const TArray<FName>& StrikeBones, const FVector& Forward, float ContactExtension)
{
    FStrikeInfo Info;

    const USkeleton* Skeleton = Animation ? Animation->GetSkeleton() : nullptr;
    if (!Skeleton || EndTime <= StartTime) return Info;

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

    struct FLimb
    {
        FName Bone;
        TArray<int32> Chain;    // parent-first, excluding the root so root motion doesn't count as reach
        TArray<float> Extensions;
        float Rest = 0.f;       // lowest point of the wind-up before the peak
        float Peak = -UE_MAX_FLT;
        float ContactTime = 0.f;
    };

    TArray<FLimb> Limbs;
    for (const FName& Bone : StrikeBones)
    {
        const int32 BoneIndex = RefSkeleton.FindBoneIndex(Bone);
        if (BoneIndex == INDEX_NONE) continue;

        FLimb& Limb = Limbs.AddDefaulted_GetRef();
        Limb.Bone = Bone;
        for (int32 Index = BoneIndex; Index > 0; Index = RefSkeleton.GetParentIndex(Index))
        {
            Limb.Chain.Insert(Index, 0);
        }
    }
    if (Limbs.IsEmpty()) return Info;

    auto ForwardOf = [&Forward](const UAnimSequence* Sequence, const TArray<int32>& Chain, float AnimTime)
    {
        FTransform ComponentSpace = FTransform::Identity;
        for (const int32 BoneIndex : Chain)
        {
            ComponentSpace = SampleBone(Sequence, BoneIndex, AnimTime) * ComponentSpace;
        }
        return FVector::DotProduct(ComponentSpace.GetLocation(), Forward);
    };

    const float Length = EndTime - StartTime;
    const int32 NumSamples = FMath::Max(2, FMath::CeilToInt(Length * 60.f));
    TArray<float> SampleTimes;

    for (int32 Sample = 0; Sample <= NumSamples; ++Sample)
    {
        const float Time = FMath::Min(StartTime + Length * Sample / NumSamples, EndTime - KINDA_SMALL_NUMBER);

        float AnimTime = 0.f;
        const UAnimSequence* Sequence = FindSequence(Animation, Time, AnimTime);
        if (!Sequence) continue;

        SampleTimes.Add(Time - StartTime);
        for (FLimb& Limb : Limbs)
        {
            Limb.Extensions.Add(ForwardOf(Sequence, Limb.Chain, AnimTime));
        }
    }
    if (SampleTimes.IsEmpty()) return Info;

    for (FLimb& Limb : Limbs)
    {
        // Peak extension, measured from the deepest point of the wind-up (hooks pull back before swinging)
        int32 PeakIndex = 0;
        for (int32 i = 1; i < Limb.Extensions.Num(); ++i)
        {
            if (Limb.Extensions[i] > Limb.Extensions[PeakIndex]) PeakIndex = i;
        }

        int32 RestIndex = 0;
        for (int32 i = 1; i <= PeakIndex; ++i)
        {
            if (Limb.Extensions[i] < Limb.Extensions[RestIndex]) RestIndex = i;
        }

        Limb.Peak = Limb.Extensions[PeakIndex];
        Limb.Rest = Limb.Extensions[RestIndex];

        // Contact is when the limb arrives, not the middle of it being held out
        const float Threshold = Limb.Rest + ContactExtension * (Limb.Peak - Limb.Rest);
        int32 ContactIndex = PeakIndex;
        for (int32 i = RestIndex; i <= PeakIndex; ++i)
        {
            if (Limb.Extensions[i] >= Threshold)
            {
                ContactIndex = i;
                break;
            }
        }
        Limb.ContactTime = SampleTimes[ContactIndex];
    }

    // The striking limb reaches furthest and moves the most. Reach alone picks hand plants (flips),
    // movement alone picks the stepping foot in punches - weighting both gets every attack right.
    auto StrikeScore = [](const FLimb& Limb) { return Limb.Peak + 0.5f * (Limb.Peak - Limb.Rest); };

    const FLimb* Striker = nullptr;
    for (const FLimb& Limb : Limbs)
    {
        if (!Striker || StrikeScore(Limb) > StrikeScore(*Striker))
        {
            Striker = &Limb;
        }
    }
    if (!Striker || Striker->Peak - Striker->Rest < 20.f) return Info;

    Info.bValid = true;
    Info.ContactTime = Striker->ContactTime;
    Info.Bone = Striker->Bone;
    Info.Reach = Striker->Peak;

    // How far the animation itself travels before contact
    float StartAnimTime = 0.f;
    float ContactAnimTime = 0.f;
    const UAnimSequence* StartSequence = FindSequence(Animation, StartTime, StartAnimTime);
    const UAnimSequence* ContactSequence = FindSequence(Animation, StartTime + Info.ContactTime, ContactAnimTime);
    if (StartSequence && StartSequence == ContactSequence)
    {
        const FVector RootTravel = SampleBone(StartSequence, 0, ContactAnimTime).GetLocation() -
            SampleBone(StartSequence, 0, StartAnimTime).GetLocation();
        Info.RootTravelToContact = FVector::DotProduct(RootTravel, Forward);
    }

    return Info;
}
