local bulletManager = nil

local function refresh_manager()
    if obj == nil then
        return nil
    end

    bulletManager = obj:GetBallisticBulletManagerComponent()
    return bulletManager
end

local function print_wind(label)
    local manager = bulletManager or refresh_manager()
    if manager == nil then
        print("[SniperWindDebug] no bullet manager")
        return
    end

    local wind = manager:GetWindAcceleration()
    print(string.format(
        "[SniperWindDebug] %s enabled=%s wind=(%.2f, %.2f, %.2f)",
        label,
        tostring(manager:IsWindEnabled()),
        wind.X,
        wind.Y,
        wind.Z
    ))
end

function BeginPlay()
    refresh_manager()
    print_wind("ready")
end

function EnableDefaultWind()
    local manager = bulletManager or refresh_manager()
    if manager == nil then
        return
    end

    manager:SetWindEnabled(true)
    manager:SetWindAcceleration(FVector(0.0, 1.5, 0.0))
    print_wind("default")
end

function ApplyCrosswindLeft()
    local manager = bulletManager or refresh_manager()
    if manager == nil then
        return
    end

    manager:SetWindEnabled(true)
    manager:SetWindAcceleration(FVector(0.0, -3.0, 0.0))
    print_wind("left")
end

function ApplyCrosswindRight()
    local manager = bulletManager or refresh_manager()
    if manager == nil then
        return
    end

    manager:SetWindEnabled(true)
    manager:SetWindAcceleration(FVector(0.0, 3.0, 0.0))
    print_wind("right")
end

function DisableWind()
    local manager = bulletManager or refresh_manager()
    if manager == nil then
        return
    end

    manager:SetWindEnabled(false)
    print_wind("disabled")
end
