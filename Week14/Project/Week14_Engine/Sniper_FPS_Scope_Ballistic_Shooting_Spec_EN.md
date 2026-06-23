# Sniper FPS / Scope / Ballistic Shooting System Implementation Specification

## 1. Goal

Implement a sniper FPS player system for Apple Jam Engine.

The player should be able to aim with the mouse, use a scope zoom, experience aim sway and recoil, and fire ballistic projectiles.  
Bullets should not be instant-hit raycasts. Instead, each bullet should be spawned as a projectile with velocity and gravity, then updated over time.

To prevent fast bullets from passing through targets, collision should be checked between the previous bullet position and the current bullet position using a segment trace or sweep trace.

This system does not directly handle enemy death, scoring, ragdoll, or slow motion.  
Instead, when a bullet hits something, it should generate `SniperHitInfo` and send it to external systems.

---

## 2. Implementation Scope

### Included Features

- FPS camera rotation
- Pitch angle clamping
- Right-click scope zoom
- Reduced mouse sensitivity while scoped
- Left-click firing
- Projectile bullet spawning
- Bullet velocity
- Gravity-based bullet drop
- Bullet lifetime management
- Bullet collision detection
- Weapon recoil
- Aim sway
- Hold breath
- Ammunition type data
- Hit information event
- Debug trajectory and hit visualization

### Excluded Features

Do not implement the following in this system:

- Enemy AI
- Enemy death animation
- Ragdoll activation
- Slow motion presentation
- Score calculation
- Frontline gauge
- Phase system
- Radio subtitles
- Air support sequence
- Advanced scope UI
- Sniper Elite-style kill cam

---

## 3. Main Classes / Components

### SniperPlayer

Responsible for player input and camera control.

Responsibilities:

- Collect input state
- Update camera rotation
- Update scope state
- Update aim sway
- Update hold breath state
- Send fire requests to the weapon component

---

### SniperWeaponComponent

Responsible for weapon state and firing.

Responsibilities:

- Manage current ammunition type
- Manage fire cooldown
- Request bullet spawn
- Generate recoil values
- Request final aim direction
- Forward hit events to external systems

---

### BallisticBulletManager

Responsible for managing active bullets.

Responsibilities:

- Spawn bullets
- Update bullet positions
- Apply gravity
- Check bullet collision
- Remove expired bullets
- Draw debug trajectories

---

### BallisticBullet

Represents one active bullet in the world.

Responsibilities:

- Store current position
- Store previous position
- Store velocity
- Store damage
- Store ammunition type
- Store lifetime
- Store collision radius
- Store alive/dead state

---

## 4. Data Structures

### SniperInputState

Fields:

- `MouseDeltaX`
- `MouseDeltaY`
- `IsFirePressed`
- `IsScopeHeld`
- `IsHoldBreathHeld`
- `IsReloadPressed`
- `IsSwitchAmmoPressed`

Purpose:

Stores the player input state for the current frame.

---

### ScopeState

Fields:

- `IsScoped`
- `NormalFOV`
- `ScopedFOV`
- `CurrentFOV`
- `TargetFOV`
- `NormalSensitivity`
- `ScopedSensitivity`
- `CurrentSensitivity`
- `ScopeBlendSpeed`

Purpose:

Manages scope enter/exit state and camera FOV interpolation.

---

### AimSwayState

Fields:

- `Time`
- `BaseSwayAmount`
- `ScopedSwayAmount`
- `CurrentSwayPitch`
- `CurrentSwayYaw`
- `BreathMultiplier`
- `HoldBreathGauge`
- `MaxHoldBreathGauge`
- `HoldBreathRecoverSpeed`
- `HoldBreathConsumeSpeed`

Purpose:

Manages breathing sway and hold breath effects.

---

### RecoilState

Fields:

- `CurrentRecoilPitch`
- `CurrentRecoilYaw`
- `RecoilRecoverSpeed`
- `LastShotRecoilPitch`
- `LastShotRecoilYaw`

Purpose:

Manages recoil created by firing and recovery over time.

---

### AmmoBallisticData

Fields:

- `AmmoType`
- `InitialSpeed`
- `GravityScale`
- `Damage`
- `BulletRadius`
- `LifeTime`
- `FireInterval`
- `RecoilPitch`
- `RecoilYawRandomRange`
- `CanDamageArmor`

Purpose:

Defines ballistic, damage, and recoil values per ammunition type.

Default ammo types:

1. `Normal`
   - Used against infantry
   - Medium bullet speed
   - Medium recoil
   - Cannot damage armored targets

2. `AntiMaterial`
   - Used against armored or heavy targets
   - Higher bullet speed
   - Higher damage
   - Stronger recoil
   - Limited ammunition
   - Can damage armored targets

---

### BallisticBullet

Fields:

- `Position`
- `PreviousPosition`
- `Velocity`
- `Damage`
- `Radius`
- `LifeTime`
- `AmmoType`
- `Owner`
- `IsAlive`
- `WasScopedShot`
- `CanDamageArmor`

Purpose:

Stores the state of one active bullet in the world.

---

### SniperHitInfo

Fields:

- `HitActor`
- `HitLocation`
- `HitNormal`
- `ShotDirection`
- `Damage`
- `AmmoType`
- `IsScopedShot`
- `IsHeadshot`
- `IsArmorPiercing`
- `Shooter`

Purpose:

Stores hit information that is sent to external gameplay systems.

---

## 5. Input Handling

Input mapping:

- Mouse X: camera yaw rotation
- Mouse Y: camera pitch rotation
- Left mouse button: fire
- Right mouse button: scope zoom
- Shift or Space: hold breath
- R: reload or ammo reset
- 1: select regular round
- 2: select anti-material round

Pseudocode:

```text
Every frame:
    InputState.MouseDeltaX = read mouse delta x
    InputState.MouseDeltaY = read mouse delta y
    InputState.IsFirePressed = check left mouse pressed this frame
    InputState.IsScopeHeld = check right mouse held
    InputState.IsHoldBreathHeld = check hold breath key held
    InputState.IsReloadPressed = check reload pressed this frame
    InputState.IsSwitchAmmoPressed = check ammo switch pressed this frame
```

---

## 6. Frame Update Order

SniperPlayer tick order:

```text
Tick(deltaTime):
    ReadInput()
    UpdateScope(deltaTime)
    UpdateHoldBreath(deltaTime)
    UpdateAimSway(deltaTime)
    UpdateRecoil(deltaTime)
    UpdateLookRotation(deltaTime)
    UpdateCameraFOV(deltaTime)
    UpdateWeapon(deltaTime)
```

BallisticBulletManager tick order:

```text
Tick(deltaTime):
    For each bullet:
        Store previous position
        Apply gravity to velocity
        Integrate position
        Perform segment collision test from previous position to current position

        If hit:
            Build SniperHitInfo
            Broadcast hit event
            Mark bullet dead

        Decrease lifetime

        If lifetime <= 0:
            Mark bullet dead

    Remove dead bullets
```

---

## 7. FPS Camera Rotation Pseudocode

```text
UpdateLookRotation(deltaTime):
    sensitivity = ScopeState.CurrentSensitivity

    yawDelta = InputState.MouseDeltaX * sensitivity
    pitchDelta = InputState.MouseDeltaY * sensitivity

    AimYaw += yawDelta
    AimPitch -= pitchDelta

    AimPitch = clamp(AimPitch, MinPitch, MaxPitch)

    finalYaw = AimYaw + AimSwayState.CurrentSwayYaw + RecoilState.CurrentRecoilYaw
    finalPitch = AimPitch + AimSwayState.CurrentSwayPitch + RecoilState.CurrentRecoilPitch

    CameraRotation = rotation(finalPitch, finalYaw, 0)
```

Notes:

- Pitch must be clamped.
- Mouse Y sign may need to be adjusted depending on engine convention.
- Aim sway and recoil are added to the final camera rotation.
- The visible camera direction and actual bullet direction should not diverge significantly.

---

## 8. Scope Zoom Pseudocode

```text
UpdateScope(deltaTime):
    if InputState.IsScopeHeld:
        ScopeState.IsScoped = true
        ScopeState.TargetFOV = ScopeState.ScopedFOV
        ScopeState.CurrentSensitivity = ScopeState.ScopedSensitivity
    else:
        ScopeState.IsScoped = false
        ScopeState.TargetFOV = ScopeState.NormalFOV
        ScopeState.CurrentSensitivity = ScopeState.NormalSensitivity
```

```text
UpdateCameraFOV(deltaTime):
    ScopeState.CurrentFOV = lerp(
        ScopeState.CurrentFOV,
        ScopeState.TargetFOV,
        deltaTime * ScopeState.ScopeBlendSpeed
    )

    Camera.SetFOV(ScopeState.CurrentFOV)
```

Recommended default values:

```text
NormalFOV = 70
ScopedFOV = 15
NormalSensitivity = 1.0
ScopedSensitivity = 0.25
ScopeBlendSpeed = 12
```

---

## 9. Aim Sway Pseudocode

```text
UpdateAimSway(deltaTime):
    AimSwayState.Time += deltaTime

    if ScopeState.IsScoped:
        baseAmount = AimSwayState.ScopedSwayAmount
    else:
        baseAmount = AimSwayState.BaseSwayAmount

    amount = baseAmount * AimSwayState.BreathMultiplier

    AimSwayState.CurrentSwayYaw = sin(Time * 1.7) * amount
    AimSwayState.CurrentSwayPitch = cos(Time * 1.2) * amount
```

Recommended default values:

```text
BaseSwayAmount = 0.02 degrees
ScopedSwayAmount = 0.08 degrees
```

---

## 10. Hold Breath Pseudocode

```text
UpdateHoldBreath(deltaTime):
    if InputState.IsHoldBreathHeld and ScopeState.IsScoped and HoldBreathGauge > 0:
        HoldBreathGauge -= deltaTime * HoldBreathConsumeSpeed
        BreathMultiplier = 0.3
    else:
        HoldBreathGauge += deltaTime * HoldBreathRecoverSpeed
        BreathMultiplier = 1.0

    HoldBreathGauge = clamp(HoldBreathGauge, 0, MaxHoldBreathGauge)
```

Recommended default values:

```text
MaxHoldBreathGauge = 3.0 seconds
HoldBreathConsumeSpeed = 1.0
HoldBreathRecoverSpeed = 0.5
```

---

## 11. Recoil Pseudocode

```text
ApplyRecoil(ammoData):
    RecoilState.CurrentRecoilPitch += ammoData.RecoilPitch
    RecoilState.CurrentRecoilYaw += random(
        -ammoData.RecoilYawRandomRange,
        ammoData.RecoilYawRandomRange
    )
```

```text
UpdateRecoil(deltaTime):
    CurrentRecoilPitch = lerp(CurrentRecoilPitch, 0, deltaTime * RecoilRecoverSpeed)
    CurrentRecoilYaw = lerp(CurrentRecoilYaw, 0, deltaTime * RecoilRecoverSpeed)
```

Recommended default values:

```text
Normal.RecoilPitch = 1.2 degrees
Normal.RecoilYawRandomRange = 0.25 degrees

AntiMaterial.RecoilPitch = 2.5 degrees
AntiMaterial.RecoilYawRandomRange = 0.5 degrees

RecoilRecoverSpeed = 8
```

---

## 12. Firing Pseudocode

```text
UpdateWeapon(deltaTime):
    Decrease fire cooldown

    if InputState.IsFirePressed:
        if fire cooldown <= 0:
            Fire()
```

```text
Fire():
    ammoData = get current ammo data

    muzzlePosition = get weapon muzzle world position
    fireDirection = GetFinalAimDirection()

    bullet.Position = muzzlePosition
    bullet.PreviousPosition = muzzlePosition
    bullet.Velocity = fireDirection * ammoData.InitialSpeed
    bullet.Damage = ammoData.Damage
    bullet.Radius = ammoData.BulletRadius
    bullet.LifeTime = ammoData.LifeTime
    bullet.AmmoType = ammoData.AmmoType
    bullet.Owner = this player
    bullet.IsAlive = true
    bullet.WasScopedShot = ScopeState.IsScoped
    bullet.CanDamageArmor = ammoData.CanDamageArmor

    BulletManager.SpawnBullet(bullet)

    ApplyRecoil(ammoData)

    Reset fire cooldown using ammoData.FireInterval
```

Notes:

- The fire direction should use the final camera aim direction.
- If the weapon muzzle position and camera direction diverge, close-range shots may feel inaccurate.
- For the minimum viable version, the muzzle position can be placed near the camera.
- A later improvement can align the muzzle shot with the center camera ray.

---

## 13. Final Aim Direction Pseudocode

```text
GetFinalAimDirection():
    finalYaw = AimYaw + AimSwayYaw + RecoilYaw
    finalPitch = AimPitch + AimSwayPitch + RecoilPitch

    return DirectionVectorFromRotation(finalPitch, finalYaw, 0)
```

Notes:

- The bullet direction should match the visible camera direction.
- If aim sway is applied visually, it should also affect the bullet direction.

---

## 14. Bullet Update Pseudocode

```text
UpdateBullet(bullet, deltaTime):
    bullet.PreviousPosition = bullet.Position

    gravity = WorldGravity * bullet.GravityScale
    bullet.Velocity += gravity * deltaTime

    bullet.Position += bullet.Velocity * deltaTime

    CheckBulletCollision(bullet, bullet.PreviousPosition, bullet.Position)

    bullet.LifeTime -= deltaTime

    if bullet.LifeTime <= 0:
        bullet.IsAlive = false
```

Gravity default:

```text
If the engine uses meters:
    WorldGravity = (0, 0, -9.8)

If the engine uses centimeters:
    WorldGravity = (0, 0, -980)
```

---

## 15. Bullet Collision Pseudocode

```text
CheckBulletCollision(bullet, start, end):
    hit = PhysicsWorld.SweepSphere(start, end, bullet.Radius)

    if hit does not exist:
        hit = PhysicsWorld.Raycast(start, end)

    if hit exists:
        hitInfo.HitActor = hit.Actor
        hitInfo.HitLocation = hit.Location
        hitInfo.HitNormal = hit.Normal
        hitInfo.ShotDirection = normalize(bullet.Velocity)
        hitInfo.Damage = bullet.Damage
        hitInfo.AmmoType = bullet.AmmoType
        hitInfo.IsScopedShot = bullet.WasScopedShot
        hitInfo.IsArmorPiercing = bullet.CanDamageArmor
        hitInfo.Shooter = bullet.Owner
        hitInfo.IsHeadshot = determine by hit bone or collider tag

        BroadcastSniperHit(hitInfo)

        bullet.IsAlive = false
```

Notes:

- Fast bullets can pass through targets if only the current position is checked.
- Always test the segment from previous position to current position.
- If `SweepSphere` is not available, use a segment raycast first.
- If headshot detection is not implemented yet, set `IsHeadshot` to false.
- Allied/enemy distinction can be handled by external systems.

---

## 16. Default Ammunition Values

```text
Normal Ammo:
    InitialSpeed = 900 or 90000 depending on engine unit
    GravityScale = 1.0
    Damage = 100
    BulletRadius = 2
    LifeTime = 5
    FireInterval = 1.0
    RecoilPitch = 1.2
    RecoilYawRandomRange = 0.25
    CanDamageArmor = false

AntiMaterial Ammo:
    InitialSpeed = 1200 or 120000 depending on engine unit
    GravityScale = 0.9
    Damage = 300
    BulletRadius = 3
    LifeTime = 5
    FireInterval = 1.5
    RecoilPitch = 2.5
    RecoilYawRandomRange = 0.5
    CanDamageArmor = true
```

Notes:

- If the engine uses centimeters as world units, speed and gravity values must be converted accordingly.
- For the game jam, gameplay feel is more important than realistic ballistic values.
- If bullet drop is too strong, reduce `GravityScale` or increase `InitialSpeed`.

---

## 17. External System Interface

This system only sends hit results to external gameplay systems.

Event example:

```text
OnSniperHit(SniperHitInfo hitInfo)
```

External systems should handle:

- Enemy health reduction
- Friendly fire judgment
- Score increase or decrease
- Ragdoll activation
- Slow motion presentation
- Kill feed
- Armored vehicle damage

This system must not directly implement those features.

---

## 18. Debug Features

Provide the following debug features during development:

```text
Debug Draw:
    - Camera aim direction line
    - Weapon muzzle position
    - Bullet trajectory line
    - Hit location point
    - Current FOV
    - Current ammo type
    - Current hold breath gauge
    - Number of active bullets
```

Example debug output:

```text
[Sniper]
Scoped: true
FOV: 15.3
Ammo: Normal
Breath: 2.4 / 3.0
Alive Bullets: 2
Last Hit: EnemySoldier_03
```

---

## 19. Acceptance Criteria

Required acceptance criteria:

1. The player can control an FPS camera with the mouse.
2. Pitch rotation is clamped.
3. Holding right mouse button smoothly decreases FOV.
4. Mouse sensitivity is reduced while scoped.
5. Pressing left mouse button spawns a bullet.
6. The bullet moves using velocity and gravity.
7. Bullet drop is visible at long range.
8. The bullet performs collision detection between previous and current position.
9. When a bullet hits something, `SniperHitInfo` is created.
10. A hit event is sent to external systems.
11. Firing creates recoil, and recoil recovers over time.
12. Aim sway exists while scoped.
13. Holding breath reduces aim sway.
14. Regular ammo and anti-material ammo are separated by data.

Additional acceptance criteria:

1. The system can be connected to a distance display UI.
2. The system can be connected to a scope reticle UI.
3. Headshot detection can be handled using a bone name or collider tag.
4. Bullet trajectory can be visualized using debug draw.

---

## 20. Implementation Priority

### Step 1

- Create `SniperPlayer`
- Implement mouse look
- Implement pitch clamp
- Log left-click fire input
- Implement right-click scope FOV change

### Step 2

- Create `SniperWeaponComponent`
- Define `AmmoBallisticData`
- Handle fire interval
- Handle fire request

### Step 3

- Create `BallisticBulletManager`
- Spawn bullets
- Move bullets
- Apply gravity
- Remove expired bullets

### Step 4

- Add segment raycast or sphere sweep collision
- Create `SniperHitInfo`
- Broadcast `OnSniperHit`

### Step 5

- Add scope sensitivity reduction
- Add aim sway
- Add hold breath
- Add recoil

### Step 6

- Add regular ammo / anti-material ammo switching
- Add debug visualization
- Tune parameters

---

## 21. Codex Implementation Notes

- Follow the existing Actor/Component architecture of the engine.
- Do not create a new input system if one already exists.
- Reuse the existing physics raycast API if available.
- If `SweepSphere` is not available, implement collision using a segment raycast first.
- Reuse the existing debug draw system if available.
- Reuse the existing camera component structure if available.
- Do not modify renderer, ragdoll, scoring, or enemy AI code.
- Implement in small buildable steps.
- Keep the project buildable after each step.
