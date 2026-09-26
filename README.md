# Arkham-Style Freeflow Combat (Unreal Engine 5.6, C++)

A melee combat system inspired by the Batman: Arkham series, built in C++ with Unreal Engine 5.6.

## Features

- **Freeflow targeting** – attacks pick the enemy in the stick direction and chain between enemies
- **Contact-accurate attacks** – contact frame, striking limb and reach are read from the animation's bone data,
  so every hit lands on the frame the fist or foot arrives
- **Motion warping from code** – travelling attacks keep their root motion and are bent onto the target at runtime
- **Data-driven attacks** – every attack (player combo, flashy kicks, counter, enemy punches) is an `AttackData` asset
- **Counters** – enemies telegraph with a flashing ring; countering plays a paired animation on both characters
- **Enemy coordination** – surround positions and attack pacing that scales with the number of enemies
- **Game feel** – hit-stop, camera shake, knockback, and slow motion on the final blow

## Project layout

| Folder | Contents |
|---|---|
| `Source/ArkhamCombat/Combat` | Combat component, strike analysis, attack data, anim notifies |
| `Source/ArkhamCombat/Characters` | Player, enemy and shared character base |
| `Source/ArkhamCombat/AI` | Enemy manager (positioning and attack pacing) |
| `Docs/CombatSystem.md` | System guide: architecture, feature-to-code map, editor setup, tuning |

Code owns the behaviour; the editor owns the content (animations, montages, effects, input, attack data).
See [`Docs/CombatSystem.md`](Docs/CombatSystem.md) for where everything lives.

## Building

1. Clone with Git LFS installed (`git lfs install`), so the `.uasset` files download.
2. Right-click `ArkhamCombat.uproject` → **Generate Visual Studio project files**.
3. Open `ArkhamCombat.sln`, build **Development Editor | Win64**, and run.
4. Test map: `Content/ThirdPerson/Lvl_ThirdPerson`.
