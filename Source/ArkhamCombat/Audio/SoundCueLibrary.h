#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoundCueLibrary.generated.h"

class USoundCue;
class USoundWave;

// Editor helper that builds a layered, randomised Sound Cue - the same graph you would wire by hand:
//   Random (main layer)   \
//                          Mixer -> Modulator (random pitch/volume) -> Output
//   Random (detail layer) /
// Callable from Editor Utility Blueprints and editor Python.
UCLASS()
class ARKHAMCOMBAT_API USoundCueLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Fills an empty Sound Cue. Each play picks one sound from each layer (no repeats until all have played)
    // and mixes them, the detail layer at DetailVolume. Editor only - does nothing in a packaged game.
    UFUNCTION(BlueprintCallable, Category = "Arkham|Editor")
    static bool BuildLayeredSoundCue(USoundCue* Cue, const TArray<USoundWave*>& MainLayer, const TArray<USoundWave*>& DetailLayer,
        float DetailVolume = 0.5f, FVector2D PitchRange = FVector2D(0.92f, 1.08f), FVector2D VolumeRange = FVector2D(0.9f, 1.f));
};
