#include "UI/PlayerHUDWidget.h"
#include "Characters/ArkhamCharacterBase.h"
#include "Combat/CombatComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Animation/WidgetAnimation.h"

void UPlayerHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    Character = Cast<AArkhamCharacterBase>(GetOwningPlayerPawn());
    if (Character.IsValid())
    {
        Character->OnHealthChanged.AddDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
        HealthPercent = Character->GetHealthPercent();

        Combat = Character->FindComponentByClass<UCombatComponent>();
        if (Combat.IsValid())
        {
            Combat->OnComboChanged.AddDynamic(this, &UPlayerHUDWidget::HandleComboChanged);
        }
    }

    TrailPercent = HealthPercent;
    HealthBar->SetPercent(HealthPercent);
    if (HealthBarTrail)
    {
        HealthBarTrail->SetPercent(TrailPercent);
    }

    // No combo yet
    if (UWidget* Root = GetComboRoot())
    {
        Root->SetRenderOpacity(0.f);
    }
}

void UPlayerHUDWidget::NativeDestruct()
{
    if (Character.IsValid())
    {
        Character->OnHealthChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleHealthChanged);
    }
    if (Combat.IsValid())
    {
        Combat->OnComboChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleComboChanged);
    }
    Super::NativeDestruct();
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    TickCombo(InDeltaTime);

    if (!HealthBarTrail || TrailPercent <= HealthPercent) return;

    if (TrailHoldRemaining > 0.f)
    {
        TrailHoldRemaining -= InDeltaTime;
    }
    else
    {
        TrailPercent = FMath::Max(HealthPercent, TrailPercent - TrailDrainSpeed * InDeltaTime);
    }
    HealthBarTrail->SetPercent(TrailPercent);
}

// ---------------------------------------------------------------------------
// Health
// ---------------------------------------------------------------------------

void UPlayerHUDWidget::HandleHealthChanged(float CurrentHealth, float MaxHealth)
{
    HealthPercent = MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f;
    HealthBar->SetPercent(HealthPercent);

    // Losing health starts (or restarts) the hold; healing just moves the trail up with the bar
    if (HealthPercent < TrailPercent)
    {
        TrailHoldRemaining = TrailDelay;
    }
    else
    {
        TrailPercent = HealthPercent;
    }
}

// ---------------------------------------------------------------------------
// Combo meter
// ---------------------------------------------------------------------------

UWidget* UPlayerHUDWidget::GetComboRoot() const
{
    return ComboGroup ? ComboGroup.Get() : ComboText.Get();
}

void UPlayerHUDWidget::HandleComboChanged(int32 ComboCount)
{
    UWidget* Root = GetComboRoot();
    if (!ComboText || !Root) return;

    // Broken: grey out and fade (only if it was showing)
    if (ComboCount == 0)
    {
        if (Root->GetRenderOpacity() > 0.f)
        {
            ComboText->SetColorAndOpacity(FSlateColor(BrokenColor));
            FadeElapsed = 0.f;
        }
        return;
    }

    if (ComboCount < MinComboToShow) return;

    ComboText->SetText(FText::Format(ComboFormat, FText::AsNumber(ComboCount)));
    ComboText->SetColorAndOpacity(FSlateColor(ComboCount >= HighlightThreshold ? HighlightColor : ComboColor));
    Root->SetRenderOpacity(1.f);
    FadeElapsed = -1.f;

    // The Arkham thump: every hit punches the number up, then it settles
    if (ComboPop)
    {
        PlayAnimation(ComboPop);
    }
    else
    {
        PopElapsed = 0.f;
    }
}

void UPlayerHUDWidget::TickCombo(float DeltaTime)
{
    UWidget* Root = GetComboRoot();
    if (!Root) return;

    if (ComboTimerBar && Combat.IsValid())
    {
        ComboTimerBar->SetPercent(Combat->GetComboTimeRemainingPercent());
    }

    if (PopElapsed >= 0.f)
    {
        PopElapsed += DeltaTime;
        const float Alpha = FMath::Clamp(PopElapsed / FMath::Max(PopDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);
        // Starts big and eases out, so the hit reads instantly
        const float Scale = FMath::Lerp(PopScale, 1.f, FMath::InterpEaseOut(0.f, 1.f, Alpha, 3.f));
        Root->SetRenderScale(FVector2D(Scale));
        if (Alpha >= 1.f)
        {
            PopElapsed = -1.f;
        }
    }

    if (FadeElapsed >= 0.f)
    {
        FadeElapsed += DeltaTime;
        const float Alpha = FMath::Clamp(FadeElapsed / FMath::Max(FadeOutDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);
        Root->SetRenderOpacity(1.f - Alpha);
        if (Alpha >= 1.f)
        {
            FadeElapsed = -1.f;
        }
    }
}
