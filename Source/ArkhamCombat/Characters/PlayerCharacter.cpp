#include "Characters/PlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Characters/EnemyCharacter.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/App.h"
#include "Blueprint/UserWidget.h"

APlayerCharacter::APlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // --- Spring Arm ---
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 300.f;
    SpringArm->SocketOffset = FVector(0.f, 50.f, 20.f);
    SpringArm->bUsePawnControlRotation = true;

    // Smooths out the attack approach so the camera doesn't jerk with every lunge
    SpringArm->bEnableCameraLag = true;
    SpringArm->CameraLagSpeed = 10.f;
    SpringArm->CameraLagMaxDistance = 200.f;
    SpringArm->bEnableCameraRotationLag = false;

    // --- Camera ---
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;

    // --- Movement ---
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);

    //Combat
    CombatComponent = CreateDefaultSubobject<UCombatComponent>(TEXT("CombatComponent"));
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // Register input mapping context
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        // Force mouse capture
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;

        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            
            if (CombatMappingContext)
            {
                Subsystem->AddMappingContext(CombatMappingContext, 0);
            }
            Subsystem->AddMappingContext(DefaultMappingContext, 1);
            if (MouseLookMappingContext)
            {
                Subsystem->AddMappingContext(MouseLookMappingContext, 2);
            }
        }
    }

    if (CombatComponent)
    {
        CombatComponent->OnFinalBlow.AddDynamic(this, &APlayerCharacter::HandleFinalBlow);
    }

    if (APlayerController* PC = Cast<APlayerController>(GetController()); PC && HUDWidgetClass)
    {
        if (UUserWidget* HUD = CreateWidget<UUserWidget>(PC, HUDWidgetClass))
        {
            HUD->AddToViewport();
        }
    }
}

void APlayerCharacter::HandleFinalBlow(AActor* Victim)
{
    FinalBlowVictim = Victim;
    FinalBlowCameraTimeLeft = FinalBlowCameraDuration;
}

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // DeltaTime is 0 during hit pause and tiny during slow-mo
    UpdateCombatCamera(FApp::GetDeltaTime());
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
        EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
        EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::MouseLook);
        EIC->BindAction(AttackAction, ETriggerEvent::Started, this, &APlayerCharacter::Attack);
        EIC->BindAction(CounterAction, ETriggerEvent::Started, this, &APlayerCharacter::Counter);
    }
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D MovementVector = Value.Get<FVector2D>();
    if (!Controller) return;

    const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
    const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

    if (CombatComponent)
    {
        // Camera-relative, same space the player steers in - used for target selection
        CombatComponent->SetMovementInput(
            ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X);

        if (CombatComponent->IsMovementLocked()) return;
    }

    AddMovementInput(ForwardDirection, MovementVector.Y);
    AddMovementInput(RightDirection, MovementVector.X);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookVector = Value.Get<FVector2D>() * GamepadLookSensitivity * GetWorld()->GetDeltaSeconds();
    AddControllerYawInput(LookVector.X);
    AddControllerPitchInput(LookVector.Y);
}

void APlayerCharacter::MouseLook(const FInputActionValue& Value)
{
    const FVector2D LookVector = Value.Get<FVector2D>() * MouseLookSensitivity;
    AddControllerYawInput(LookVector.X);
    AddControllerPitchInput(LookVector.Y);
}

void APlayerCharacter::Attack()
{
    if (CombatComponent)
    {
        CombatComponent->TryAttack();
    }
}

void APlayerCharacter::Counter()
{
    if (CombatComponent)
    {
        CombatComponent->TryCounter();
    }
}

void APlayerCharacter::TakeCombatHit(const FCombatHit& Hit, AActor* DamageInstigator)
{
    if (bIsDead) return;
    if (CombatComponent && CombatComponent->IsInvulnerable())
    {
        UE_LOG(LogTemp, Log, TEXT("Hit from %s ignored - player is countering"), DamageInstigator ? *DamageInstigator->GetName() : TEXT("?"));
        return;
    }

    Super::TakeCombatHit(Hit, DamageInstigator);

    if (CombatComponent)
    {
        CombatComponent->ReceiveHit(DamageInstigator, Hit);
    }
}

void APlayerCharacter::Die()
{
    Super::Die();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        DisableInput(PC);
    }

    GetWorld()->GetTimerManager().SetTimer(RestartTimer, this, &APlayerCharacter::RestartLevel, RestartDelay, false);
}

void APlayerCharacter::RestartLevel()
{
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}

void APlayerCharacter::UpdateCombatCamera(float RealDeltaTime)
{
    if (!SpringArm) return;

    const FVector PlayerLocation = GetActorLocation();

    int32 NearbyCount = 0;
    FVector NearbySum = FVector::ZeroVector;
    float FurthestDistance = 0.f;

    for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
    {
        if (It->IsDead()) continue;

        const float Distance = FVector::Dist2D(PlayerLocation, It->GetActorLocation());
        if (Distance > CombatEngageRadius) continue;

        ++NearbyCount;
        NearbySum += It->GetActorLocation();
        FurthestDistance = FMath::Max(FurthestDistance, Distance);
    }

    // Enter combat immediately, leave only after a delay so the camera doesn't pump
    if (NearbyCount > 0)
    {
        bInCombat = true;
        TimeSinceEnemyNearby = 0.f;
    }
    else
    {
        TimeSinceEnemyNearby += RealDeltaTime;
        if (TimeSinceEnemyNearby >= CombatExitDelay)
        {
            bInCombat = false;
        }
    }

    float TargetArmLength = ExploreArmLength;
    FVector TargetFraming = FVector::ZeroVector;

    if (bInCombat)
    {
        // Pull back as the fight spreads out so every engaged enemy stays in frame
        TargetArmLength = FMath::Clamp(
            CombatArmLength + FurthestDistance * CombatSpreadArmScale,
            CombatArmLength, CombatMaxArmLength);

        if (NearbyCount > 0)
        {
            // TargetOffset is world space, so this shifts the pivot toward the group
            // no matter which way the camera is facing
            FVector ToGroupCenter = NearbySum / NearbyCount - PlayerLocation;
            ToGroupCenter.Z = 0.f;
            TargetFraming = (ToGroupCenter * FramingWeight).GetClampedToMaxSize(MaxFramingOffset);
        }
    }

    float ArmSpeed = ArmLengthInterpSpeed;
    float FramingSpeed = FramingInterpSpeed;

    // Final blow overrides the framing to push in on the victim
    if (FinalBlowCameraTimeLeft > 0.f)
    {
        FinalBlowCameraTimeLeft -= RealDeltaTime;
        TargetArmLength = FinalBlowArmLength;
        ArmSpeed = FramingSpeed = FinalBlowInterpSpeed;

        if (const AActor* Victim = FinalBlowVictim.Get())
        {
            FVector ToVictim = Victim->GetActorLocation() - PlayerLocation;
            ToVictim.Z = 0.f;
            TargetFraming = (ToVictim * FinalBlowFramingWeight).GetClampedToMaxSize(MaxFramingOffset);
        }
    }

    SpringArm->TargetArmLength = FMath::FInterpTo(
        SpringArm->TargetArmLength, TargetArmLength, RealDeltaTime, ArmSpeed);

    SpringArm->TargetOffset = FMath::VInterpTo(
        SpringArm->TargetOffset, TargetFraming, RealDeltaTime, FramingSpeed);
}
