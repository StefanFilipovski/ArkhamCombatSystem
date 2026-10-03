#include "Audio/SoundCueLibrary.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "EdGraph/EdGraphNode.h"

#if WITH_EDITOR
namespace
{
    // Lay nodes out left to right like a hand-made graph, so the cue reads well when opened
    void PlaceNode(USoundNode* Node, int32 X, int32 Y)
    {
        if (UEdGraphNode* GraphNode = Node->GetGraphNode())
        {
            GraphNode->NodePosX = X;
            GraphNode->NodePosY = Y;
        }
    }

    // A Random node with one Wave Player per sound
    USoundNodeRandom* BuildRandomLayer(USoundCue* Cue, const TArray<USoundWave*>& Waves, int32 TopY)
    {
        USoundNodeRandom* Random = Cue->ConstructSoundNode<USoundNodeRandom>();
        Random->bRandomizeWithoutReplacement = true;
        while (Random->ChildNodes.Num() < Waves.Num())
        {
            Random->InsertChildNode(Random->ChildNodes.Num());
        }

        for (int32 i = 0; i < Waves.Num(); ++i)
        {
            USoundNodeWavePlayer* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>();
            Player->SetSoundWave(Waves[i]);
            Random->ChildNodes[i] = Player;
            PlaceNode(Player, -900, TopY + i * 110);
        }

        PlaceNode(Random, -560, TopY + (Waves.Num() - 1) * 55);
        return Random;
    }
}
#endif

bool USoundCueLibrary::BuildLayeredSoundCue(USoundCue* Cue, const TArray<USoundWave*>& MainLayer, const TArray<USoundWave*>& DetailLayer,
    float DetailVolume, FVector2D PitchRange, FVector2D VolumeRange)
{
#if WITH_EDITOR
    if (!Cue || MainLayer.IsEmpty() || Cue->FirstNode)
    {
        return false;
    }

    Cue->Modify();

    USoundNodeModulator* Modulator = Cue->ConstructSoundNode<USoundNodeModulator>();
    Modulator->PitchMin = PitchRange.X;
    Modulator->PitchMax = PitchRange.Y;
    Modulator->VolumeMin = VolumeRange.X;
    Modulator->VolumeMax = VolumeRange.Y;
    if (Modulator->ChildNodes.IsEmpty())
    {
        Modulator->InsertChildNode(0);
    }
    PlaceNode(Modulator, -180, 0);

    USoundNodeRandom* Main = BuildRandomLayer(Cue, MainLayer, -300);

    if (DetailLayer.IsEmpty())
    {
        Modulator->ChildNodes[0] = Main;
    }
    else
    {
        USoundNodeRandom* Detail = BuildRandomLayer(Cue, DetailLayer, -300 + MainLayer.Num() * 110 + 80);

        USoundNodeMixer* Mixer = Cue->ConstructSoundNode<USoundNodeMixer>();
        while (Mixer->ChildNodes.Num() < 2)
        {
            Mixer->InsertChildNode(Mixer->ChildNodes.Num());
        }
        Mixer->ChildNodes[0] = Main;
        Mixer->ChildNodes[1] = Detail;
        Mixer->InputVolume[0] = 1.f;
        Mixer->InputVolume[1] = DetailVolume;
        PlaceNode(Mixer, -360, 0);

        Modulator->ChildNodes[0] = Mixer;
    }

    Cue->FirstNode = Modulator;
    Cue->LinkGraphNodesFromSoundNodes();
    Cue->PostEditChange();
    Cue->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
