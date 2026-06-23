local sniperPawn = nil
local weapon = nil
local bulletManager = nil

local function refresh_refs()
    if obj == nil then
        return false
    end

    sniperPawn = obj
    weapon = sniperPawn:GetSniperWeaponComponent()
    bulletManager = sniperPawn:GetBallisticBulletManagerComponent()
    return sniperPawn ~= nil
end

local function print_status(label)
    if sniperPawn == nil and not refresh_refs() then
        print("[SniperStatusDebug] no sniper pawn")
        return
    end

    local ammoType = weapon ~= nil and weapon:GetCurrentAmmoType() or -1
    local cooldown = weapon ~= nil and weapon:GetFireCooldownRemaining() or 0.0
    local zeroRange = weapon ~= nil and weapon:GetZeroRangeMeters() or 0.0
    local windEnabled = bulletManager ~= nil and bulletManager:IsWindEnabled() or false
    local wind = bulletManager ~= nil and bulletManager:GetWindAcceleration() or FVector(0.0, 0.0, 0.0)

    print(string.format(
        "[SniperStatusDebug] %s scoped=%s scopeAlpha=%.2f breath=%.2f/%.2f active=%s ammo=%d cooldown=%.2f zero=%.1f wind=%s (%.2f, %.2f, %.2f)",
        label,
        tostring(sniperPawn:IsScoped()),
        sniperPawn:GetScopeBlendAlpha(),
        sniperPawn:GetHoldBreathGauge(),
        sniperPawn:GetMaxHoldBreathGauge(),
        tostring(sniperPawn:IsHoldBreathActive()),
        ammoType,
        cooldown,
        zeroRange,
        tostring(windEnabled),
        wind.X,
        wind.Y,
        wind.Z
    ))
end

function BeginPlay()
    refresh_refs()
    print_status("ready")
end

function PrintStatus()
    print_status("manual")
end
