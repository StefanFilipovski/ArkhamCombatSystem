# Arkham-Style Freeflow Combat — System Guide

Where everything lives, how it fits together, and what was changed in assets.
All gameplay code is C++ under `Source/ArkhamCombat/`. To jump to a function in Visual Studio, press Ctrl+T and type its name.

---

## 0. The split: code owns behaviour, the editor owns content

| Lives in C++ (`Source/`) | Lives in the editor (`Content/`) |
|---|---|
| *How* things work: targeting, combo flow, hit detection, counters, AI pacing | *What* is used: animations, montages, effects, camera shakes, input actions, meshes, materials |
| *When* things happen: contact frames (read from the animation), hit windows, timers | Montage setup: sections, blend times, slot, auto blend out, notifies |
| Components and their default behaviour settings (ranges, speeds, damage) | Which assets fill each slot, and per-Blueprint tuning of those settings |

Rules the code follows:
- **No asset paths in C++.** Every asset is a `UPROPERTY` assigned in a Blueprint.
- **Attacks are data.** Every attack (player combo, flashy kicks, counter, enemy punches) is an `AttackData`
  asset: montage + section + how hard it hits. The combo is an ordered list of them — add, remove or reorder
  entries in the Blueprint to change it. Code decides *which* attack and *when*; the asset decides *what* and *how hard*.
- **Blends and slots belong to the montage.** Code just calls `Montage_Play` / `PlayAnimMontage`.

### Editor tour — where each asset is plugged in

**`Content/Characters/Player/BP_PlayerCharacter`** → *Class Defaults*
- Input: `Default Mapping Context`, `Move/Look/Mouse Look/Attack/Counter Action`, `Mouse Look Mapping Context`
- Keys live in the mapping contexts, not the Blueprint: `Content/Variant_Combat/Input/IMC_Combat` maps
  `IA_ComboAttack` (left mouse / right bumper) and `IA_Counter` (`Content/Input/Actions`, Pressed trigger).
  `IMC_Combat` is added by `BP_CombatPlayerController`; the character adds `IMC_Default` and `IMC_MouseLook` on top.

**`BP_PlayerCharacter` → select `CombatComponent`** in the Components panel
- Combat|Attacks: `Combo Attacks` (DA_Punch01, DA_Punch02, DA_KickFinisher — in order), `Flashy Attacks` (DA_Flashy_*)
- Combat|Counter: `Counter Attack` (DA_Counter_PalmStrike), `Counter Pair Distance` (how far apart the pair is lined up)
- `AM_PlayerCounter` has a **Counter Impact** notify (Branching Point) on the frame the palm lands — move it to change when the counter damage hits
- Combat|Hit Reactions: `Hit React Montages` (AM_PlayerHitReact01/02), `Death Montage` (AM_PlayerDeath), `Player Hit Camera Shake`
- Combat|Impact: `Hit Camera Shake`, `Hit Effect` (NS_Damage), `Hit Sound`

**`Content/Characters/Enemies/BP_EnemyCharacter`** → *Class Defaults*
- Combat|Animations: `Hit React Montage`, `Stagger Montage Large`, `Death Montage`, `Hit React Sections`, `Stagger Sections`
- Combat|Attack: `Attacks` (DA_EnemyJab/StraightPunch/HookPunch), `Countered Montage` (AM_EnemyCountered)

**Attack Data assets** — `Content/Characters/Player/Attacks/DA_*`, `Content/Characters/Enemies/Attacks/DA_*`
- Animation: `Montage`, `Section` (None = whole montage)
- Hit: `Damage`, `Knockdown Chance` (0 = flinch, 1 = always knocked down), `Knockback Scale`, `Only Hits Target`, `Hit Socket Override`
- Feel: `Hit Pause Duration` (freeze frame), `Camera Shake Scale`
- Movement: `Fallback Strike Distance` (only used if the reach can't be read from the animation)
- Create new ones: Content Browser → right-click → *Miscellaneous* → *Data Asset* → **AttackData**

**`BP_EnemyCharacter` → select `AttackIndicatorRing`**
- `Static Mesh` (engine Plane), `Materials[0]` (M_AttackIndicator). Any material works as long as it has an `Urgency` scalar parameter.

**`Content/Characters/Enemies/Materials/M_AttackIndicator`** — the ring's whole look
- Code only sets `Urgency` (0 when the ring appears → 1 when the punch lands) and places the ring.
- The material turns that into: colour `StartColor → EndColor`, flash rate `FlashRateStart → FlashRateEnd`
  (flashes per second, via Time → Sine), brightness `MinIntensity → MaxIntensity` on each flash.

**Montages** (open one, check the *Asset Details* panel): `Blend In` / `Blend Out` / `Enable Auto Blend Out`
- Enemy: `Content/Characters/Enemies/Animations/AM_Enemy*`
- Player: `Content/Characters/Player/PlayerAnimations/AM_Player*` — `AM_PlayerDeath` has auto blend out **off** so the player stays down

---

## 1. Architecture at a glance

```
ABP / Montages ──notifies──┐
                           ▼
APlayerCharacter ──input──► UCombatComponent ──hits──► AEnemyCharacter
  (camera, input,            (player combat brain)       (reactions, attacks,
   health/death)                   ▲                       counter reaction, indicator)
                                   │ IsEngaging()                ▲
                                   │                             │ StartAttack()
AArkhamGameMode ── owns ──► UEnemyManagerComponent ──────────────┘
                              (surround slots, attack pacing)

StrikeAnalysis (shared) ── reads bone data from animations for both sides
```

| Class | File | Role |
|---|---|---|
| `AArkhamCharacterBase` | `Characters/ArkhamCharacterBase.*` | Health, `TakeCombatHit`, `Die` — shared by player and enemies |
| `APlayerCharacter` | `Characters/PlayerCharacter.*` | Input binding, combat camera, final-blow camera, player hit/death |
| `UCombatComponent` | `Combat/CombatComponent.*` | Everything the player does in a fight |
| `AEnemyCharacter` | `Characters/EnemyCharacter.*` | Hit reactions, knockback, attacking, being countered, attack indicator |
| `UEnemyManagerComponent` | `AI/EnemyManagerComponent.*` | On the game mode; surround positions and who attacks when |
| `StrikeAnalysis` | `Combat/StrikeAnalysis.*` | Finds contact frame / striking limb / reach from animation bones |
| Anim notifies | `Combat/AnimNotify_*`, `AnimNotifyState_HitWindow` | Legacy hit windows, death pose (the auto strike analysis now drives timing) |

---

## 2. Feature → code map

### Player attacking (`Combat/CombatComponent.cpp`)
| Feature | Function(s) |
|---|---|
| Attack input, buffering, cancel windows | `TryAttack`, `SetMovementInput` |
| Target selection (stick direction → current → facing → any) | `SelectTarget`, `FindTargetInDirection`, `IsTargetable` |
| Outline on next target | `UpdateTargetHighlight` |
| Choosing the attack: next in `ComboAttacks`, or a `FlashyAttacks` entry on target switch / far target / killing finisher | `StartAttack`, `PickFlashyAttack` |
| What an attack is and how hard it hits (data asset + the `FCombatHit` passed to the victim) | `Combat/AttackData.h` |
| Reading contact frame + striking limb from the animation | `AnalyzeStrike` → `StrikeAnalysis::Analyze` |
| Code-driven approach (slide timed to contact) | `UpdateApproach`, `UpdateFacing` |
| Motion warping (root motion bent onto the target, added at runtime) | `AddWarpModifiers`, `UpdateWarpTarget`, `ClearWarpModifiers` |
| Movement lock / unlock during attacks | `LockMovement`, `UnlockMovement` |
| Hit window, swept hit detection | `OpenHitWindow`, `CloseHitWindow`, `UpdateHitDetection` |
| Guaranteed hit on the committed target at contact | `OnContact`, `ApplyGuaranteedHit` |
| Applying damage, retargeting off staggered/dead enemies | `ApplyHit` |
| Ignoring notifies from blending-out montages | `IsCurrentAttackNotify`, `OnAttackMontageEnded` |

### Impact / game feel (`Combat/CombatComponent.cpp`)
| Feature | Function(s) |
|---|---|
| Hit-stop (freeze frames), tiered punch/kick/flashy | `TriggerHitPause`, `EndHitPause` |
| Camera shake + Niagara hit effect + optional sound | `PlayHitImpact` |
| Final-blow slow motion | `IsLastEnemyStanding`, `StartFinalBlow`, `EndFinalBlow` |
| Final-blow camera push-in | `APlayerCharacter::HandleFinalBlow`, `UpdateCombatCamera` (`Characters/PlayerCharacter.cpp`) |
| Enemy knockback on light hits | `AEnemyCharacter::UpdateKnockback` (`Characters/EnemyCharacter.cpp`) |

### Counter (`Combat/CombatComponent.cpp` + `Characters/EnemyCharacter.cpp`)
| Feature | Function(s) |
|---|---|
| Right-click input | `APlayerCharacter::Counter` → `UCombatComponent::TryCounter` |
| Pick the enemy whose punch lands soonest, in range | `TryCounter` |
| Align the pair, play both halves of the paired animation | `StartCounter`, `AEnemyCharacter::PlayCountered` |
| Damage when the **Counter Impact** notify fires (placed in `AM_PlayerCounter`), cancel windows | `AnimNotify_CounterImpact` → `OnCounterImpact`, `ApplyCounterImpact`, `UpdateCounter`, `EndCounter`, `CanLeaveCounter` |
| Invulnerable while countering | `IsInvulnerable` (header), checked in `APlayerCharacter::TakeCombatHit` |

### Enemy attacks (`Characters/EnemyCharacter.cpp`)
| Feature | Function(s) |
|---|---|
| Can this enemy attack now (not hit recently, not staggered…) | `CanStartAttack` |
| Pick punch, slow it to fill the counter window, telegraph | `StartAttack` |
| Swing + homing approach, contact, recovery | `BeginStrike`, `UpdateAttack`, `ResolveAttackContact`, `EndAttack` |
| Interrupted by being hit | `CancelAttack`, called from `TakeCombatHit` |
| Ring indicator placement + feeding `Urgency` to the material (the look is in the material) | `SetIndicatorVisible`, `UpdateIndicator` |

### Enemy coordination (`AI/EnemyManagerComponent.cpp`)
| Feature | Function(s) |
|---|---|
| Surround slots + separation | `CalculateSurroundPositions`, `CalculateSeparationForce`, `UpdateEnemyPositions` |
| Attack pacing scaled by enemy count, attacker slots, skip the enemy you're hitting | `UpdateAttacks` |

### Player being hit (`Combat/CombatComponent.cpp` + `Characters/PlayerCharacter.cpp`)
| Feature | Function(s) |
|---|---|
| Damage entry point, invulnerability check, death + level restart | `APlayerCharacter::TakeCombatHit`, `Die`, `RestartLevel` |
| Stagger animation, hit stun, knockdown pose | `UCombatComponent::ReceiveHit`, `EndHitStun`, `PlayReaction` |

### Camera & input (`Characters/PlayerCharacter.cpp`)
| Feature | Function(s) |
|---|---|
| Combat framing (arm length by enemy spread, pivot toward group) | `UpdateCombatCamera` |
| Mouse + gamepad look, mapping context priorities | `BeginPlay`, `Look`, `MouseLook` |

---

## 3. How strike analysis works (`Combat/StrikeAnalysis.cpp`)

Instead of trusting hand-placed notifies, every attack animation is sampled at 60 Hz:

1. For each candidate limb (`hand_r`, `hand_l`, `foot_r`, `foot_l`), compute its position in
   component space relative to the root, projected on the character's forward axis.
2. Find the peak extension, and the deepest wind-up point before it (hooks pull back first).
3. **Contact** = first frame the limb is `ContactExtension` (0.85) of the way from wind-up to peak.
4. **Striking limb** = highest score of `peak + 0.5 × (peak − wind-up)` — reach alone picks hand
   plants in flips, movement alone picks the stepping foot in punches.
5. **Reach** sets how far from the target to stand; **root travel before contact** decides
   whether the attack is motion-warped (≥ 50 units) or uses the code slide.

Results are logged at startup: search the Output Log for `Strike ` and `Enemy strike`.

---

## 4. Asset changes made by editor script

These were done with Unreal's editor Python API (commandlet / `-ExecutePythonScript`), not by hand.

| Change | Assets | Why |
|---|---|---|
| Enabled **Root Motion** | `Characters/Player/PlayerAnimations/H2H_*_Forward` (4 kicks) | Travel was drawn as mesh offset → overshoot and snap-back |
| Enabled **Root Motion** | `Characters/Enemies/Animations/H2H_StaggerLarge01-03` | Same snap-back on enemy staggers |
| **Retargeted** UE4 → UE5 mannequin with `Characters/Anims/RTG_UE4ToUE5`, root motion enabled | `Enemies/Animations/Attacks/*` (jab, straight, hook), `Enemies/Animations/Counter/Paired_CounterPunch_PalmStrike_Att`, `Player/PlayerAnimations/Counter/Paired_CounterPunch_PalmStrike_Vic`, `Player/PlayerAnimations/HitReacts/*` | Source anims (RamsterZ pack) are on the UE4 skeleton |
| **Created material** | `Characters/Enemies/Materials/M_AttackIndicator` | Additive, unlit, no depth test; ring drawn by a Custom HLSL node |
| **Created montages** | `Enemies/Animations/AM_EnemyJab`, `AM_EnemyStraightPunch`, `AM_EnemyHookPunch`, `AM_EnemyCountered`; `Player/PlayerAnimations/AM_PlayerCounter`, `AM_PlayerHitReact01/02`, `AM_PlayerDeath` | Blends, slot and auto blend out live in the asset instead of code |
| **Assigned Blueprint defaults** | `BP_PlayerCharacter` (+ its CombatComponent), `BP_EnemyCharacter` (+ its AttackIndicatorRing) | All content references listed in the editor tour above |

Backups of the originals: `C:\Portfolio\Projects\ArkhamCombat_Asset_Backup_2026-09-24\`.
Original source backup: `C:\Portfolio\Projects\ArkhamCombat_Source_Backup_2026-09-24\`.

---

## 5. Where to tune things

| Blueprint | Component / category | Examples |
|---|---|---|
| `BP_PlayerCharacter` | CombatComponent → Combat\|Targeting, Movement, Strike, Hit, Impact, Counter, Hit Reactions | `LungeRange`, `CloseAttackRange`, `bUseMotionWarping`, `ContactExtension`, `CounterPairDistance`, `CounterCancelDelay`, `HitStunDuration` |
| `BP_PlayerCharacter` | Camera | `CombatArmLength`, `CombatMaxArmLength`, `FramingWeight`, `FinalBlowArmLength` |
| `DA_*` Attack Data assets | Hit, Feel | `Damage`, `KnockdownChance`, `KnockbackScale`, `HitPauseDuration`, `CameraShakeScale` — per attack |
| `BP_EnemyCharacter` | Combat\|Attack, Indicator, Knockback | `CounterWindow`, `AttackLockoutAfterHit`, `bIndicatorOnStrikingHand`, `HitKnockbackDistance` |
| `BP_ArkhamGameMode` | EnemyManager → AI\|Attacks | `SoloAttackInterval`, `CrowdAttackInterval`, `EnemiesPerAttacker`, `bEnemiesAttack` |
