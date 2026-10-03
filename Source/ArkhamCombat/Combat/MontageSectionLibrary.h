#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MontageSectionLibrary.generated.h"

class UAnimMontage;

// Editor helper for cutting a multi-hit montage into one section per hit - the same as adding sections by hand
// in the montage editor. Callable from Editor Utility Blueprints and editor Python.
UCLASS()
class ARKHAMCOMBAT_API UMontageSectionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Replaces the montage's sections with one per name, starting at the matching time (seconds), unlinked so each plays on its own.
    // Editor only - does nothing in a packaged game.
    UFUNCTION(BlueprintCallable, Category = "Arkham|Editor")
    static bool SetMontageSections(UAnimMontage* Montage, const TArray<FName>& SectionNames, const TArray<float>& StartTimes);
};
