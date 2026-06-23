local totalHits = 0
local totalScore = 0

function BeginPlay()
    totalHits = 0
    totalScore = 0
    print("[SniperHitDebug] ready")
end

function OnSniperHit(hitInfo)
    totalHits = totalHits + 1
    totalScore = totalScore + math.floor(hitInfo.Damage)

    print(string.format(
        "[SniperHitDebug] hits=%d score=%d damage=%.1f dist=%.1f speed=%.1f outcome=%d ammo=%d scoped=%s armor=%s ragdoll=%s",
        totalHits,
        totalScore,
        hitInfo.Damage,
        hitInfo.TravelDistance,
        hitInfo.ImpactSpeed,
        hitInfo.HitOutcome,
        hitInfo.AmmoType,
        tostring(hitInfo.bIsScopedShot),
        tostring(hitInfo.bIsArmorPiercing),
        tostring(hitInfo.bShouldRagdoll)
    ))
end
