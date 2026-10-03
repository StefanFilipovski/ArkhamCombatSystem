#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UWidgetAnimation;
class AArkhamCharacterBase;
class UCombatComponent;

// Logic for the player's HUD. The look lives in a Widget Blueprint made from this class (WBP_PlayerHUD).
// Widgets are found by name: "HealthBar" (required), and optionally "HealthBarTrail", "ComboText", "ComboTimerBar", "ComboGroup".
UCLASS(Abstract)
class ARKHAMCOMBAT_API UPlayerHUDWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // --- Health ---
    // Current health
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> HealthBar;

    // Optional: shows the chunk just lost, holds for a moment, then drains down to the health bar
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> HealthBarTrail;

    // Seconds the lost chunk stays visible before draining
    UPROPERTY(EditAnywhere, Category = "Health")
    float TrailDelay = 0.6f;

    // How fast the trail drains, in bar fractions per second
    UPROPERTY(EditAnywhere, Category = "Health")
    float TrailDrainSpeed = 0.5f;

    // --- Combo meter ---
    // The combo number
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ComboText;

    // Optional: time left before the combo breaks
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> ComboTimerBar;

    // Optional: a panel holding the combo widgets - it gets the punch and the fade. Without it, ComboText does.
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> ComboGroup;

    // Optional: a widget animation named "ComboPop" played on every hit instead of the built-in punch
    UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
    TObjectPtr<UWidgetAnimation> ComboPop;

    // {0} is the hit count
    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    FText ComboFormat = NSLOCTEXT("PlayerHUD", "ComboFormat", "x{0}");

    // The meter appears from this many hits
    UPROPERTY(EditAnywhere, Category = "Combo Meter", meta = (ClampMin = "1"))
    int32 MinComboToShow = 2;

    // Built-in punch: the number jumps to this scale on a hit and settles back to 1
    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    float PopScale = 1.6f;

    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    float PopDuration = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    float FadeOutDuration = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    FLinearColor ComboColor = FLinearColor::White;

    // From HighlightThreshold hits on, the number switches to this colour
    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    FLinearColor HighlightColor = FLinearColor(1.f, 0.75f, 0.1f);

    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    int32 HighlightThreshold = 8;

    // Colour the number turns as it fades out when the combo breaks
    UPROPERTY(EditAnywhere, Category = "Combo Meter")
    FLinearColor BrokenColor = FLinearColor(0.5f, 0.5f, 0.5f);

private:
    UFUNCTION()
    void HandleHealthChanged(float CurrentHealth, float MaxHealth);

    UFUNCTION()
    void HandleComboChanged(int32 ComboCount);

    UWidget* GetComboRoot() const;
    void TickCombo(float DeltaTime);

    TWeakObjectPtr<AArkhamCharacterBase> Character;
    TWeakObjectPtr<UCombatComponent> Combat;

    float HealthPercent = 1.f;
    float TrailPercent = 1.f;
    float TrailHoldRemaining = 0.f;

    // Time since the last pop / since the combo broke; negative = not running
    float PopElapsed = -1.f;
    float FadeElapsed = -1.f;
};
