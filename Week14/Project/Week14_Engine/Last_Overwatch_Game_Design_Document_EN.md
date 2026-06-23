# Game Design Document Draft

## 1. Game Overview

### Working Title

**Last Overwatch**

### Genre

FPS / Sniping Action

### Platform

PC

### Engine

Apple Jam Engine

### Estimated Playtime

5–7 minutes

### Core Concept

The player takes the role of a rear-line sniper positioned away from the main battlefield.  
Instead of directly joining the ground combat, the player observes the battlefield from a distance and eliminates high-priority threats to keep allied forces alive until air support arrives.

Allied ground troops and enemy ground troops are already engaged in combat.  
The player must support the allied frontline by identifying key enemy targets, accounting for bullet drop, controlling scope sway, and making precise shots.

The player is generally safe from ordinary ground troops due to their third-party overwatch position.  
However, special threats such as suicide drones can directly target the player, forcing them to balance battlefield support with self-preservation.

---

## 2. Design Intent

This game is designed as the final game jam project using our custom game engine.  
The goal is to demonstrate engine features in an actual playable game rather than as isolated technical demos.

The game is designed to naturally showcase the following engine systems:

- FPS camera and input system
- Projectile-based ballistic shooting
- Scope zoom and FOV control
- Aim sway and weapon recoil
- Ragdoll-based hit reactions
- Slow motion kill presentation
- Physics-based collision response
- UI-based battlefield information
- Lighting, shadows, fog, and post-processing for battlefield atmosphere

Instead of building a full movement-based FPS where the player fights every enemy directly, the game focuses on a rear-line overwatch sniper role.  
This keeps the scope achievable within the short development period while still producing a complete game experience.

---

## 3. Game Objective

The player's main objective is to **hold the allied frontline until air support arrives**.

The game is played under a time limit.  
If the allied frontline gauge remains above zero when the timer ends, the player wins.

The player earns points by eliminating enemy infantry, special units, armored targets, and suicide drones.  
Shooting allied units causes a score penalty and negatively affects the allied frontline.

---

## 4. Core Gameplay Loop

The basic gameplay loop is:

1. Observe the battlefield through the scope.
2. Identify allied and enemy units.
3. Determine which enemy target is the greatest threat to the frontline.
4. Aim while accounting for distance, bullet drop, and scope sway.
5. Fire and confirm the hit result.
6. Score and frontline status change based on the result.
7. New threats appear as the phase progresses.
8. Survive until air support arrives.

The core fun of the game is not simply shooting enemies quickly.  
It comes from reading the battlefield, choosing the right target priority, and landing precise long-range shots.

---

## 5. Game Rules

### Basic Rules

- The game lasts approximately 5–7 minutes.
- The game is divided into 3–4 phases.
- The player operates from a fixed or limited sniper position.
- Allied and enemy forces continuously fight on the battlefield.
- Battlefield status is displayed at the top of the screen.
- If the allied frontline gauge reaches zero, the player loses.
- If the timer expires and air support arrives, the player wins.

### Scoring Rules

- Kill a regular enemy: gain points.
- Kill a special enemy: gain higher points.
- Destroy an armored target: gain a large amount of points.
- Shoot down a suicide drone: gain points and remove a direct threat.
- Kill an allied unit: lose points.
- Bonus points may be awarded for headshots, long-range shots, or consecutive hits.

### Lose Conditions

- The allied frontline gauge reaches zero.
- The player is killed by a direct threat such as a suicide drone.
- A required defense objective fails before air support arrives.

### Win Condition

- The player survives until the timer ends.
- The allied frontline remains active when air support arrives.

---

## 6. Player Role

The player is a rear-line sniper.

The player does not directly participate in ground combat.  
Ordinary enemy infantry will not move all the way to the player's sniper position.  
Instead, the player observes the battlefield from a third-party overwatch position and intervenes through long-range precision shooting.

However, the player is not completely safe.  
From later phases onward, suicide drones or special threats may directly approach the player's position.  
This forces the player to make decisions between supporting the frontline and protecting themselves.

---

## 7. Core Systems

### 7.1 FPS Control System

The player controls the view using the mouse.

Core controls:

- Mouse X: yaw rotation
- Mouse Y: pitch rotation
- Pitch angle clamp
- Left mouse button: fire
- Right mouse button: scope zoom
- Hold breath key: reduce aim sway
- Ammo switch key: switch ammunition type

Player movement should be minimized.  
The core of this game is observation and sniping, not movement-based combat.  
A fixed sniper point should be prioritized for the minimum viable version.

### 7.2 Scope / Zoom System

Holding the right mouse button enters scope mode.

In scope mode:

- Camera FOV decreases.
- Mouse sensitivity decreases.
- A central crosshair is displayed.
- Aim sway becomes more noticeable.
- Distance and bullet drop become more important.

The scope is one of the most important feel-based systems in the game.  
Smooth FOV transition and reduced sensitivity should create the feeling of handling a sniper rifle.

### 7.3 Ballistics System

Bullets are not instant-hit raycasts.  
Instead, each shot spawns a projectile that travels through the world.

Each projectile is affected by:

- Initial velocity
- Fire direction
- Gravity
- Lifetime
- Collision detection

Because of this, the player must account for bullet drop when shooting distant targets.  
The player may need to aim slightly above the target depending on distance.

### 7.4 Aim Sway / Recoil System

A slight breathing sway affects the player's aim.

In scope mode, this sway becomes more noticeable.  
The player can temporarily reduce sway by holding their breath.

Recoil occurs when firing.

- Regular rounds have moderate recoil.
- Anti-material rounds have stronger recoil.
- Recoil gradually recovers over time.

This system adds tension to aiming and encourages the player to take careful, deliberate shots.

### 7.5 Ammunition System

The game should use at least two ammunition types.

#### Regular Round

The default ammunition type for infantry targets.

- Medium bullet speed
- Moderate recoil
- Effective against infantry
- Ineffective against armored targets

#### Anti-Material Round

A limited ammunition type used against armored or heavy targets.

- Higher bullet speed
- Higher damage
- Stronger recoil
- Can damage armored targets
- Limited ammo count

Instead of implementing multiple weapons, different ammunition types are used to express tactical choices in a simpler way.

### 7.6 Allied / Enemy Identification

Both allied and enemy units exist on the battlefield.

The player must identify targets before firing.  
Shooting an allied unit causes a score penalty and may also reduce the frontline status.

Identification can be supported through:

- Unit colors
- Silhouette differences
- Overhead markers
- Simple target identification UI while scoped

Friendly fire is allowed, but the player must be given enough information to distinguish allies from enemies fairly.

### 7.7 Frontline Gauge System

The top of the screen displays the battlefield status.

The frontline gauge gives the player a clear reason to manage the battle instead of simply shooting random enemies.

Possible UI labels:

- Allied Force
- Enemy Pressure
- Frontline Stability

If key enemy units remain alive, the allied frontline gauge decreases faster.  
Eliminating high-priority enemies slows the gauge loss or temporarily stabilizes the frontline.

If the frontline gauge reaches zero, the player loses.

### 7.8 Major Kill Presentation

Special presentation effects occur when the player lands an important shot.

Trigger examples:

- Headshot
- Long-range hit
- Armored vehicle destruction
- Drone shootdown
- Key phase target elimination

Presentation elements:

- Short slow motion
- Ragdoll activation
- Camera shake
- Kill feed message
- Bonus score display

Instead of applying complex presentation to every enemy death, the game should focus these effects on important kills.

### 7.9 Suicide Drone

The suicide drone is a direct threat to the player.

Ordinary ground troops do not directly attack the player.  
However, suicide drones approach the player's sniper position and explode if they get close enough.

The drone's role is to:

- Prevent the player from staying scoped indefinitely
- Force the player to switch between battlefield support and self-defense
- Increase tension in later phases

### 7.10 Ultimate / Mortar Support

The player may have access to limited mortar or artillery support.

This system should remain simple:

- Activated with a specific key
- Targets the aimed location or a selected area
- Damages enemies in a wide radius
- Has a long cooldown
- Can damage armored targets

This is not a required core feature.  
It should be implemented only if time remains after the main systems are complete.

---

## 8. Phase Structure

The game consists of 3–4 phases.

### Phase 1: Battlefield Entry

Goal: Teach basic controls and sniping feel.

Elements:

- Regular enemy infantry
- Allied infantry
- Low frontline pressure
- Simple shooting targets

The player learns scope control, bullet drop, and allied/enemy identification.

### Phase 2: High-Priority Targets

Goal: Encourage target priority decisions.

Elements:

- Heavy gunners
- Officers
- Enemy units that heavily pressure the allied frontline
- Longer-distance targets

The player must stop shooting random enemies and instead eliminate targets that have the greatest impact on the frontline.

### Phase 3: Direct Player Threats

Goal: Add pressure through self-defense.

Elements:

- Suicide drones
- Faster-moving enemies
- Armored vehicles or heavy targets
- Anti-material rounds become necessary

The player must support the battlefield while also protecting themselves.

### Phase 4: Waiting for Air Support

Goal: Final pressure and victory buildup.

Elements:

- Larger number of enemies
- High frontline pressure
- Remaining ammo and ultimate usage become important
- Countdown to air support

When the timer ends, air support arrives and clears the remaining enemies as the game ends.

---

## 9. Enemy Types

### Regular Infantry

The basic enemy type.

- Low health
- Can be killed with regular rounds
- Low score value
- Can appear in large numbers

### Heavy Gunner

A dangerous enemy that heavily damages the allied frontline.

- Higher priority than regular infantry
- Increases frontline pressure if left alive
- Worth more points

### Officer / Commander

A support-type enemy that strengthens enemy pressure.

- May have low direct combat power
- Increases enemy pressure while alive
- Stabilizes the frontline when eliminated

### Armored Vehicle

A durable target that is difficult to damage with regular rounds.

- High health
- Requires anti-material rounds or mortar support
- Greatly damages the allied frontline if ignored

### Suicide Drone

A direct threat to the player.

- Moves toward the player's position
- Explodes when close enough
- Grants score when destroyed
- Creates tension in later phases

---

## 10. UI Layout

### Top UI

- Remaining time
- Allied frontline gauge
- Enemy pressure gauge or status
- Current phase

### Center UI

- Crosshair
- Scope mask
- Bullet drop marks
- Target distance display

### Bottom UI

- Current ammunition type
- Remaining ammo
- Hold breath gauge
- Mortar cooldown
- Current score

### Event UI

- Headshot message
- Long-range hit message
- Friendly fire warning
- Drone approach warning
- Air support countdown

---

## 11. Sound / Presentation Direction

Sound should reinforce battlefield atmosphere and player feedback.

Required sounds:

- Rifle shot
- Bullet impact
- Enemy hit
- Armored impact
- Drone approach
- Explosion
- Radio voice or radio effect

Radio subtitles can be used to deliver information to the player.

Example radio lines:

- “Enemy heavy gunner spotted. The frontline is under pressure.”
- “Drone incoming. Break scope and defend yourself.”
- “Air support arrives in 60 seconds.”
- “Friendly fire. Confirm your target.”

---

## 12. Visual Direction

The visual style should emphasize battlefield tension and the isolated feeling of a rear-line sniper.

Visual elements:

- Distant smoke and fog
- Explosion effects
- Scope view
- Dark and tense battlefield color palette
- Strong silhouettes for key targets
- Slow motion hit presentation
- Ragdoll reactions

Lighting, shadows, fog, particles, and ragdoll effects should be used actively to showcase the custom engine.

---

## 13. Development Priority

### Priority 1: Required

- Player FPS camera
- Scope zoom
- Left-click firing
- Projectile bullets
- Bullet drop
- Bullet collision
- Enemy/allied hit distinction
- Scoring system
- Time limit
- Frontline gauge
- Basic phase progression

### Priority 2: Game Feel and Completeness

- Aim sway
- Hold breath
- Recoil
- Anti-material rounds
- Suicide drones
- High-priority targets
- Radio subtitles
- Basic sound effects

### Priority 3: Presentation Polish

- Slow motion
- Ragdoll kill presentation
- Mortar support
- Armored vehicle destruction
- Improved scope UI
- Distance display
- Air support arrival sequence

---

## 14. Minimum Viable Version

The minimum viable version is complete when the following conditions are met:

- The player can view the battlefield through a scope.
- Enemies and allies are placed on the battlefield.
- The player can fire projectile bullets with visible bullet drop.
- Hitting an enemy increases the score.
- Hitting an ally decreases the score.
- The frontline gauge decreases over time.
- Eliminating key enemies reduces or slows frontline pressure.
- If the frontline gauge reaches zero before the timer ends, the player loses.
- If the timer ends before the frontline collapses, air support arrives and the player wins.

This minimum version is enough to establish the core game structure.

---

## 15. Target Player Experience

The intended player experience is:

“I feel safe from a distance, but the battlefield is collapsing.  
I cannot just shoot any enemy I see.  
I need to identify which target is threatening the frontline the most.  
I look through the scope, hold my breath, account for bullet drop, and take the shot.  
One accurate shot can keep the frontline alive.”

This game is not just a shooting game.  
It is about **battlefield judgment and precision sniping from an overwatch position**.
