#pragma once

#include "CoreMinimal.h"
#include "Characters/ArkhamCharacterBase.h"
#include "InputActionValue.h"
#include "Combat/CombatComponent.h"
#include "PlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UUserWidget;

UCLASS()
class ARKHAMCOMBAT_API APlayerCharacter : public AArkhamCharacterBase
{
    GENERATED_BODY()

public:
    APlayerCharacter();

protected:
    virtual void BeginPlay() override;

    // --- Camera ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    USpringArmComponent* SpringArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    UCameraComponent* Camera;

    // Degrees per second at full stick deflection
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    float GamepadLookSensitivity = 100.f;

    // Multiplier on raw mouse delta
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    float MouseLookSensitivity = 1.f;

    // --- Combat Camera ---
    // Arm length when no enemies are around
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float ExploreArmLength = 300.f;

    // Arm length in combat with enemies right next to the player
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float CombatArmLength = 380.f;

    // Arm length cap when enemies are spread out
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float CombatMaxArmLength = 650.f;

    // Extra arm length per unit of distance to the furthest engaged enemy
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float CombatSpreadArmScale = 0.5f;

    // Enemies within this radius put the camera into combat framing
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float CombatEngageRadius = 800.f;

    // How long after the last enemy leaves before combat framing ends (prevents zoom pumping)
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float CombatExitDelay = 1.5f;

    // How far the camera pivot shifts toward the enemy group center (0 = none, 1 = fully)
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float FramingWeight = 0.35f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float MaxFramingOffset = 200.f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float ArmLengthInterpSpeed = 2.f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float FramingInterpSpeed = 3.f;

    // --- Final Blow Camera ---
    // Push in on the last enemy of the fight while the slow-mo plays
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Final Blow")
    float FinalBlowArmLength = 220.f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Final Blow")
    float FinalBlowFramingWeight = 0.6f;

    // Real-time seconds
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Final Blow")
    float FinalBlowCameraDuration = 1.5f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Final Blow")
    float FinalBlowInterpSpeed = 5.f;

    // --- Input ---
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* CombatMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* MoveAction;

    // Gamepad right stick
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* LookAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* MouseLookAction;

    // Maps the mouse to MouseLookAction. Added above the controller's contexts so it wins the Mouse2D key.
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* MouseLookMappingContext;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    UCombatComponent* CombatComponent;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* AttackAction;

    // Counter button. Which keys trigger it is set in the input mapping context, not in code.
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* CounterAction;

    // On-screen HUD (WBP_PlayerHUD, made from PlayerHUDWidget), added when the game starts
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UUserWidget> HUDWidgetClass;

    // Seconds after going down before the level restarts
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float RestartDelay = 4.f;

    // --- Input Callbacks ---
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void MouseLook(const FInputActionValue& Value);
    void Attack();
    void Counter();

public:
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    virtual void TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator) override;
    virtual void Die() override;

    // Enemies close by
    virtual bool IsInCombat() const override { return bInCombat; }

private:
    // RealDeltaTime ignores hit pause and slow-mo so the camera stays smooth through them
    void UpdateCombatCamera(float RealDeltaTime);

    UFUNCTION()
    void HandleFinalBlow(AActor* Victim);

    void RestartLevel();
    FTimerHandle RestartTimer;

    bool bInCombat = false;
    float TimeSinceEnemyNearby = 0.f;

    TWeakObjectPtr<AActor> FinalBlowVictim;
    float FinalBlowCameraTimeLeft = 0.f;
};
