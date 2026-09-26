#pragma once

#include "CoreMinimal.h"

class UAnimSequenceBase;
class UAnimMontage;

// What an attack animation does, read from its bone data
struct FStrikeInfo
{
    bool bValid = false;
    float ContactTime = 0.f;            // seconds from StartTime until the striking limb arrives (ContactExtension of the way to full extension)
    FName Bone;                         // the striking limb
    float Reach = 0.f;                  // how far in front of the root that limb gets
    float RootTravelToContact = 0.f;    // how far the animation itself moves forward before contact
};

namespace StrikeAnalysis
{
    /**
     * Finds the strike in [StartTime, EndTime) of a sequence or montage (montage time for montages).
     * Forward is the character's forward axis in mesh component space.
     */
    ARKHAMCOMBAT_API FStrikeInfo Analyze(const UAnimSequenceBase* Animation, float StartTime, float EndTime,
        const TArray<FName>& StrikeBones, const FVector& Forward, float ContactExtension = 0.85f);

    // Time range of a montage section; Section = None means the whole montage
    ARKHAMCOMBAT_API bool GetSectionRange(const UAnimMontage* Montage, FName Section, float& OutStart, float& OutEnd);
}
