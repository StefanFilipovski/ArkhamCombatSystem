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

### Content layout
Everything made for this project lives in `Content/ArkhamCombat/`; the third-party Fab pack `SciFITrooper_Man_03` stays outside it.

| Folder | Contents |
|---|---|
| `Core` | `BP_ArkhamGameMode` (also plays the background music) |
| `Maps` | `Lvl_ThirdPerson` - the main level (default map for editor and game) |
| `Input` | Mapping contexts (`IMC_Combat`, `IMC_Default`, `IMC_MouseLook`) and `Actions/` |
| `Characters/Player` | `BP_PlayerCharacter`, `Attacks/` (attack data), `Animations/` (montages, `Converted/`, `Counter/`, `HitReacts/`) |
| `Characters/Enemies` | `BP_EnemyCharacter`, `BP_EnemySpawner`, `Attacks/`, `Animations/`, `Materials/` (attack indicator) |
| `Characters/Shared` | `ABP_PlayerCharacter` (anim blueprint used by player and enemies) and shared locomotion / hit-reaction animations |
| `Characters/Mannequins` | The UE5 mannequin the animations run on (meshes, materials, rigs, template animations in use) |
| `Characters/Trooper` | IK Rig, IK Retargeter and follower anim blueprint that put the trooper model on the mannequin |
| `Characters/Retargeting` | `RTG_UE4ToUE5` + `IK_UE4Mannequin`, the `UE4Mannequin` they need, and `SourceAnimations/` (UE4 moves kept for converting later) |
| `Audio` | `Music/` and `Impacts/` (layered `SC_*` sound cues and their sounds) |
| `UI` | `WBP_PlayerHud`, `WBP_DemoComplete` |
| `FX` | `NS_Damage` hit effect, `CameraShakes/`, `Materials/` (enemy outline) |
### Editor tour — where each asset is plugged in

**`Content/ArkhamCombat/Characters/Player/BP_PlayerCharacter`** → *Class Defaults*
- Input: `Combat / Default / Mouse Look Mapping Context`, `Move/Look/Mouse Look/Attack/Counter Action`
- Keys live in the mapping contexts in `Content/ArkhamCombat/Input`, not the Blueprint: `IMC_Combat` maps
  `IA_ComboAttack` (left mouse / right bumper) and `IA_Counter` (Pressed trigger). The character adds all three
  contexts; `IMC_Default` and `IMC_MouseLook` sit above `IMC_Combat` so they decide what the mouse and sticks do.

**Character models — `VisualMesh` component** (both character Blueprints, from `AArkhamCharacterBase`)
- The main `Mesh` (UE5 mannequin) runs every animation and is what the combat code reads. When `VisualMesh` has a
  skeletal mesh, the main Mesh is hidden but keeps animating, and `VisualMesh` copies its pose.
- `VisualMesh` uses `ABP_TrooperRetarget` (a single **Retarget Pose From Mesh** node, *Use Attached Parent* on),
  which uses `RTG_Manny_To_Trooper` (IK Rigs `IK_SKM_Manny_Simple` → `IK_Trooper`). All in `Content/ArkhamCombat/Characters/Trooper`.
- `Looks` (Class Defaults → Appearance): material sets in slot order, one picked at random on spawn — enemy colour variety.
- Enemy outline is applied to whichever mesh is visible (`GetVisibleMesh`).

**`BP_PlayerCharacter` → select `CombatComponent`** in the Components panel
- Combat|Attacks: `Combo Attacks` (DA_Punch01, DA_Punch02, DA_KickFinisher — in order), `Extra Combo Chains` (more combos, each an ordered list; each chain has a `Chance` of replacing the main combo when a new combo starts. Tick `Play On Single Press` to make it a special combo: one press plays every hit), `Flashy Attacks` (DA_Flashy_*)
- Combat|Counter: `Counter Attack` (DA_Counter_PalmStrike), `Counter Pair Distance` + `Counter Pair Side Offset` (where the player stands relative to the enemy; 76 / 5 were measured from the paired animation's contact frames)
- `AM_PlayerCounter` has a **Counter Impact** notify (Branching Point) on the frame the palm lands — move it to change when the counter damage hits
- Combat|Hit Reactions: `Hit React Montages` (AM_PlayerHitReact01/02), `Hit React Montages From Behind` (played without turning around when hit from behind), `Death Montage` (AM_PlayerDeath), `Death Montage From Behind`, `From Behind Angle`, `Player Hit Camera Shake`, `Player Hit Sound` (when an enemy hits the player)
- Combat|Impact: `Hit Camera Shake`, `Hit Effect` (NS_Damage), `Hit Sound` (default for the player's hits; a Sound Cue with Random + Modulator nodes gives variety)

**`Content/ArkhamCombat/Characters/Enemies/BP_EnemyCharacter`** → *Class Defaults*
- Combat|Animations: `Hit React Montage`, `Stagger Montage Large`, `Death Montage`, `Hit React Sections`, `Stagger Sections`
- Combat|Attack: `Attacks` (DA_EnemyJab/StraightPunch/HookPunch), `Countered Montage` (AM_EnemyCountered)

**Attack Data assets** — `Content/ArkhamCombat/Characters/Player/Attacks/DA_*`, `Content/ArkhamCombat/Characters/Enemies/Attacks/DA_*`
- Animation: `Montage`, `Section` (None = whole montage)
- Hit: `Damage`, `Knockdown Chance` (0 = flinch, 1 = always knocked down), `Knockback Scale`, `Only Hits Target`, `Hit Socket Override`
- Feel: `Hit Pause Duration` (freeze frame), `Camera Shake Scale`, `Impact Sound` (optional per-attack sound, e.g. heavier for kicks; empty = the default)
- Movement: `Fallback Strike Distance` (only used if the reach can't be read from the animation)
- Create new ones: Content Browser → right-click → *Miscellaneous* → *Data Asset* → **AttackData**

**`BP_EnemyCharacter` → select `AttackIndicatorRing`**
- `Static Mesh` (engine Plane), `Materials[0]` (M_AttackIndicator). Any material works as long as it has an `Urgency` scalar parameter.

**`Content/ArkhamCombat/Characters/Enemies/Materials/M_AttackIndicator`** — the ring's whole look
- Code only sets `Urgency` (0 when the ring appears → 1 when the punch lands) and places the ring.
- The material turns that into: colour `StartColor → EndColor`, flash rate `FlashRateStart → FlashRateEnd`
  (flashes per second, via Time → Sine), brightness `MinIntensity → MaxIntensity` on each flash.

**`Content/ArkhamCombat/Characters/Enemies/BP_EnemySpawner`** → *Class Defaults* → Encounter
- `Enemy Class` (BP_EnemyCharacter) and the encounter rules: `Initial Enemies`, `Max Alive Enemies`,
  `Total Enemies`, `Defeats Per Extra Enemy`, `Respawn Delay`, `Min Spawn Distance`, `Restart Delay After Complete`
- The Blueprint's graph listens to `On Encounter Complete` and shows the end-of-demo message

**The spawner placed in `Lvl_ThirdPerson`** → *Details* → Encounter
- `Spawn Points`: the **Target Point** actors around the arena edge that enemies run in from.
  Leave empty to use random spots on the nav mesh within `Fallback Spawn Radius` of the spawner.
  Copy-pasting the spawner into another level empties this list (links to actors don't cross levels) - reassign the points after pasting.
- Numbers can be overridden per level here, the Blueprint holds the defaults.

**Montages** (open one, check the *Asset Details* panel): `Blend In` / `Blend Out` / `Enable Auto Blend Out`
- Enemy: `Content/ArkhamCombat/Characters/Enemies/Animations/AM_Enemy*`
- Player: `Content/ArkhamCombat/Characters/Player/Animations/AM_Player*` — `AM_PlayerDeath` has auto blend out **off** so the player stays down

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
                              (surround slots, attack pacing)       ▲
                                                                    │ SpawnActor, OnDied
AEnemySpawner (placed in level) ────────────────────────────────────┘
  (keeps the crowd topped up, counts defeats, ends the demo)

StrikeAnalysis (shared) ── reads bone data from animations for both sides
```

| Class | File | Role |
|---|---|---|
| `AArkhamCharacterBase` | `Characters/ArkhamCharacterBase.*` | Health, `TakeCombatHit`, `Die` — shared by player and enemies |
| `APlayerCharacter` | `Characters/PlayerCharacter.*` | Input binding, combat camera, final-blow camera, player hit/death |
| `UCombatComponent` | `Combat/CombatComponent.*` | Everything the player does in a fight |
| `AEnemyCharacter` | `Characters/EnemyCharacter.*` | Hit reactions, knockback, attacking, being countered, attack indicator |
| `UEnemyManagerComponent` | `AI/EnemyManagerComponent.*` | On the game mode; surround positions and who attacks when |
| `AEnemySpawner` | `AI/EnemySpawner.*` | Placed in the level; spawns enemies at its spawn points, counts defeats, ends the encounter |
| `UPlayerHUDWidget` | `UI/PlayerHUDWidget.*` | HUD logic: health bar + draining trail, driven by `OnHealthChanged`; the look is `WBP_PlayerHUD` |
| `StrikeAnalysis` | `Combat/StrikeAnalysis.*` | Finds contact frame / striking limb / reach from animation bones |
| `UMontageSectionLibrary` | `Combat/MontageSectionLibrary.*` | Editor helper: `SetMontageSections` cuts a multi-hit montage into one section per hit (used for the punch combo montages) |
| `USoundCueLibrary` | `Audio/SoundCueLibrary.*` | Editor helper: `BuildLayeredSoundCue` builds a Random + Random → Mixer → Modulator Sound Cue (used for `Content/ArkhamCombat/Audio/Impacts/SC_*`) |
| Anim notifies | `Combat/AnimNotify_*`, `AnimNotifyState_HitWindow` | Legacy hit windows, death pose (the auto strike analysis now drives timing) |

---

## 2. Feature → code map

### Player attacking (`Combat/CombatComponent.cpp`)
| Feature | Function(s) |
|---|---|
| Attack input, buffering, cancel windows | `TryAttack`, `SetMovementInput` |
| Target selection (stick direction → current → facing → any) | `SelectTarget`, `FindTargetInDirection`, `IsTargetable` |
| Outline on next target | `UpdateTargetHighlight` |
| Choosing the attack: next in the current combo chain (`ComboAttacks`, unless an `ExtraComboChains` entry wins its `Chance` roll when a combo starts), or a `FlashyAttacks` entry on target switch / far target / killing finisher | `StartAttack`, `PickComboChain`, `PickFlashyAttack` |
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
| Align the pair as authored (distance + side offset), let the pair pass through each other so their root motion plays out, play both halves | `StartCounter`, `SetIgnoreCounterPartner`, `AEnemyCharacter::PlayCountered` |
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

### Enemy spawning and the end of the demo (`AI/EnemySpawner.cpp`)
| Feature | Function(s) |
|---|---|
| Starting crowd (one tick after BeginPlay, once the player exists) | `SpawnInitialEnemies` |
| Crowd size: `InitialEnemies`, +1 every `DefeatsPerExtraEnemy` defeats, capped at `MaxAliveEnemies` | `GetDesiredAliveCount` |
| Replacements after a defeat, one at a time with `RespawnDelay` | `QueueSpawns`, `SpawnQueuedEnemy` |
| Spawn point choice: free, at least `MinSpawnDistance` from the player, off-screen if possible, not the same one twice | `PickSpawnPoint` (nav mesh fallback: `PickNavigableLocation`) |
| Counting defeats (via the `OnDied` event every character fires in `AArkhamCharacterBase::Die`) | `HandleEnemyDied`, `RegisterDefeat` |
| Demo end: `OnEncounterComplete` for the Blueprint/UI, then level restart | `CompleteEncounter`, `RestartLevel` |
| Final-blow slow-mo only on the encounter's last enemy, not whenever the arena is briefly empty | `UCombatComponent::IsLastEnemyStanding` asks `IsEncounterComplete` |

### Player being hit (`Combat/CombatComponent.cpp` + `Characters/PlayerCharacter.cpp`)
| Feature | Function(s) |
|---|---|
| Damage entry point, invulnerability check, death + level restart | `APlayerCharacter::TakeCombatHit`, `Die`, `RestartLevel` |
| Stagger animation (front or from behind), hit stun, knockdown pose | `UCombatComponent::ReceiveHit`, `EndHitStun`, `PlayReaction` |

### HUD (`UI/PlayerHUDWidget.cpp`)
| Feature | Function(s) |
|---|---|
| Health changes broadcast by every character | `AArkhamCharacterBase::OnHealthChanged` (fired in `TakeCombatHit`) |
| Health bar + trail that holds the lost chunk, then drains | `UPlayerHUDWidget::HandleHealthChanged`, `NativeTick` |
| Combo meter: every landed hit (and counter) adds one; breaks after `ComboTimeout` (2.5 s) without a hit, or when the player is hit | `UCombatComponent::AddComboHit`, `BreakCombo`, `OnComboChanged` |
| Combo number with the Arkham "thump" on each hit, timer bar, highlight colour from 8 hits, grey fade when it breaks (optional `ComboPop` widget animation replaces the built-in thump) | `UPlayerHUDWidget::HandleComboChanged`, `TickCombo` |
| HUD added at start (class set in `BP_PlayerCharacter` → UI → `HUD Widget Class`) | `APlayerCharacter::BeginPlay` |

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
| Enabled **Root Motion** | `Characters/Player/Animations/H2H_*_Forward` (4 kicks) | Travel was drawn as mesh offset → overshoot and snap-back |
| Enabled **Root Motion** | `Characters/Enemies/Animations/H2H_StaggerLarge01-03` | Same snap-back on enemy staggers |
| **Retargeted** UE4 → UE5 mannequin with `Characters/Retargeting/RTG_UE4ToUE5`, root motion enabled | `Enemies/Animations/Attacks/*` (jab, straight, hook), `Enemies/Animations/Counter/Paired_CounterPunch_PalmStrike_Att`, `Player/Animations/Counter/Paired_CounterPunch_PalmStrike_Vic`, `Player/Animations/HitReacts/*` | Source anims (RamsterZ pack) are on the UE4 skeleton |
| **Created material** | `Characters/Enemies/Materials/M_AttackIndicator` | Additive, unlit, no depth test; ring drawn by a Custom HLSL node |
| **Created montages** | `Enemies/Animations/AM_EnemyJab`, `AM_EnemyStraightPunch`, `AM_EnemyHookPunch`, `AM_EnemyCountered`; `Player/Animations/AM_PlayerCounter`, `AM_PlayerHitReact01/02`, `AM_PlayerDeath` | Blends, slot and auto blend out live in the asset instead of code |
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
| `BP_ArkhamGameMode` | Audio | `BackgroundMusic` (Machine_Language, Looping ticked on the sound), `MusicVolume`, `MusicFadeInTime`, `MusicFadeOutTime` (on player death) |
| `BP_EnemySpawner` (or the placed instance) | Encounter | `InitialEnemies`, `MaxAliveEnemies`, `TotalEnemies`, `DefeatsPerExtraEnemy`, `RespawnDelay`, `MinSpawnDistance` |
