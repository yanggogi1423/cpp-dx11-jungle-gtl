local animInstance = nil
local combatAgent = nil
local fireElapsed = 0.0
local lastMoveState = -1.0
local lastDeath = false
local lastEngaging = false
local hitBoolPulseTime = 0.0
local healthBillboards = nil
local lastHealthIndicatorState = ""

local FIRE_TRIGGER_INTERVAL = 0.45
local HIT_BOOL_PULSE_DURATION = 0.12
local HEALTH_INDICATOR_GREEN_RATIO = 0.60
local HEALTH_INDICATOR_ORANGE_RATIO = 0.30
local HEALTH_INDICATOR_MATERIALS = {
    green = "Content/Material/UI/AllyStatus/AllyHealth_Green.uasset",
    orange = "Content/Material/UI/AllyStatus/AllyHealth_Orange.uasset",
    red = "Content/Material/UI/AllyStatus/AllyHealth_Red.uasset"
}

local function find_anim_instance()
    if obj == nil then
        return nil
    end

    local mesh = nil
    if obj.GetSkeletalMeshComponent ~= nil then
        mesh = obj:GetSkeletalMeshComponent()
    end
    if mesh == nil and obj.GetMesh ~= nil then
        mesh = obj:GetMesh()
    end
    if mesh == nil or mesh.GetAnimInstance == nil then
        return nil
    end

    return mesh:GetAnimInstance()
end

local function get_anim_instance()
    if animInstance == nil then
        animInstance = find_anim_instance()
    end
    return animInstance
end

local function find_combat_agent()
    if obj == nil then
        return nil
    end

    if obj.GetCombatCoverAgentComponent ~= nil then
        local agent = obj:GetCombatCoverAgentComponent()
        if agent ~= nil then
            return agent
        end
    end

    if obj.GetComponents == nil then
        return nil
    end

    local components = obj:GetComponents()
    for _, component in pairs(components) do
        if component ~= nil and component.IsA ~= nil and component:IsA("UCombatCoverAgentComponent") then
            return component
        end
    end

    return nil
end

local function get_combat_agent()
    if combatAgent == nil then
        combatAgent = find_combat_agent()
    end
    return combatAgent
end

local function find_component_by_tag(tag)
    if obj == nil then
        return nil
    end

    if obj.GetComponentByTag ~= nil then
        local component = obj:GetComponentByTag(tag)
        if component ~= nil then
            return component
        end
    end
    if obj.FindComponentByTag ~= nil then
        return obj:FindComponentByTag(tag)
    end
    return nil
end

local function set_component_visible(component, visible)
    if component == nil then
        return
    end
    if component.SetVisible ~= nil then
        component:SetVisible(visible)
    elseif component.SetVisibility ~= nil then
        component:SetVisibility(visible)
    end
end

local function collect_health_billboards()
    return {
        green = find_component_by_tag("AllyHealthGreen"),
        orange = find_component_by_tag("AllyHealthOrange"),
        red = find_component_by_tag("AllyHealthRed")
    }
end

local function get_health_billboards()
    if healthBillboards == nil
        or (healthBillboards.green == nil
            and healthBillboards.orange == nil
            and healthBillboards.red == nil) then
        healthBillboards = collect_health_billboards()
    end
    return healthBillboards
end

local function update_health_indicator(agent)
    local billboards = get_health_billboards()
    if billboards == nil then
        return
    end

    local state = "hidden"
    if agent ~= nil and agent.IsAlive ~= nil and agent:IsAlive() then
        local ratio = 1.0
        if agent.GetHealthRatio ~= nil then
            ratio = tonumber(agent:GetHealthRatio()) or ratio
        elseif agent.GetHealth ~= nil and agent.GetMaxHealth ~= nil then
            local maxHealth = tonumber(agent:GetMaxHealth()) or 0.0
            if maxHealth > 0.0 then
                ratio = (tonumber(agent:GetHealth()) or 0.0) / maxHealth
            end
        end

        if ratio > HEALTH_INDICATOR_GREEN_RATIO then
            state = "green"
        elseif ratio > HEALTH_INDICATOR_ORANGE_RATIO then
            state = "orange"
        else
            state = "red"
        end
    end

    if state == lastHealthIndicatorState then
        return
    end

    lastHealthIndicatorState = state

    if billboards.green ~= nil and billboards.orange ~= nil and billboards.red ~= nil then
        set_component_visible(billboards.green, state == "green")
        set_component_visible(billboards.orange, state == "orange")
        set_component_visible(billboards.red, state == "red")
        return
    end

    set_component_visible(billboards.green, false)
    set_component_visible(billboards.orange, false)
    set_component_visible(billboards.red, false)

    if state == "hidden" then
        return
    end

    local indicator = billboards[state] or billboards.green or billboards.orange or billboards.red
    set_component_visible(indicator, true)

    if indicator ~= nil and indicator.SetMaterialPath ~= nil then
        indicator:SetMaterialPath(HEALTH_INDICATOR_MATERIALS[state])
    elseif indicator ~= nil and MaterialLibrary ~= nil and MaterialLibrary.SetComponentMaterialByPath ~= nil then
        MaterialLibrary.SetComponentMaterialByPath(indicator, 0, HEALTH_INDICATOR_MATERIALS[state])
    end
end

local function get_hit_body_name(hitInfo)
    if hitInfo == nil then
        return ""
    end
    if hitInfo.HitBodyName ~= nil and hitInfo.HitBodyName ~= "" then
        return hitInfo.HitBodyName
    end
    if hitInfo.HitBoneNameString ~= nil then
        return hitInfo.HitBoneNameString
    end
    return ""
end

local function get_hit_region_name(hitInfo)
    if hitInfo == nil then
        return "Unknown"
    end
    if hitInfo.HitRegionName ~= nil and hitInfo.HitRegionName ~= "" then
        return hitInfo.HitRegionName
    end
    return "Unknown"
end

local function get_hit_region_display_name(hitInfo)
    if hitInfo == nil then
        return "UNKNOWN"
    end
    if hitInfo.HitRegionDisplayName ~= nil and hitInfo.HitRegionDisplayName ~= "" then
        return hitInfo.HitRegionDisplayName
    end
    return string.upper(get_hit_region_name(hitInfo))
end

local function publish_sniper_event(eventName, hitInfo)
    if GameGeneralManager == nil or GameGeneralManager.Publish == nil then
        return
    end

    GameGeneralManager:Publish(eventName, {
        hit = hitInfo,
        target = obj,
        shooter = hitInfo ~= nil and hitInfo.Shooter or nil,
        killed = hitInfo ~= nil and hitInfo.bKilled == true or false,
        friendly = hitInfo ~= nil and hitInfo.bFriendlyTarget == true or false,
        hit_body_name = get_hit_body_name(hitInfo),
        hit_region_name = get_hit_region_name(hitInfo),
        hit_region_display_name = get_hit_region_display_name(hitInfo),
        hit_score_multiplier = hitInfo ~= nil and tonumber(hitInfo.HitScoreMultiplier) or 1.0,
        hit_score_value = hitInfo ~= nil and math.floor(tonumber(hitInfo.HitScoreValue) or 0) or 0
    })
end

local function set_initial_variables()
    local anim = get_anim_instance()
    if anim == nil then
        return false
    end

    anim:SetGraphVariableFloat("MoveState", 0.0)
    anim:SetGraphVariableBool("Death", false)
    if anim.SetGraphVariableBool ~= nil then
        anim:SetGraphVariableBool("Hit", false)
    end
    return true
end

local function set_graph_trigger(anim, variableName)
    if anim == nil then
        return false
    end
    if anim.SetGraphVariableTrigger ~= nil then
        local ok = anim:SetGraphVariableTrigger(variableName)
        if ok then
            return true
        end
    end
    if anim.SetGraphVariableBool ~= nil then
        return anim:SetGraphVariableBool(variableName, true)
    end
    return false
end

local function agent_is_alive(agent)
    if agent == nil then
        return true
    end
    if agent.IsAlive ~= nil then
        return agent:IsAlive()
    end
    return true
end

local function agent_role_name(agent)
    if agent == nil then
        return ""
    end
    if agent.GetResolvedCombatRoleName ~= nil then
        local resolved = agent:GetResolvedCombatRoleName()
        if resolved ~= nil and resolved ~= "" and resolved ~= "AutoFromTeam" then
            return resolved
        end
    end
    if agent.GetCombatRoleName ~= nil then
        local role = agent:GetCombatRoleName()
        if role ~= nil then
            return role
        end
    end
    return ""
end

local function should_run_during_combat_movement(agent)
    if agent == nil then
        return false
    end
    if agent.ShouldRunDuringCombatMovement ~= nil then
        return agent:ShouldRunDuringCombatMovement()
    end
    return agent_role_name(agent) == "EnemyAssault"
end

local function current_move_state(agent)
    if agent == nil then
        return 0.0
    end
    if agent.GetCombatAnimationMoveState ~= nil then
        return agent:GetCombatAnimationMoveState()
    end
    if agent.IsMovingForCombatRange ~= nil and agent:IsMovingForCombatRange() then
        return should_run_during_combat_movement(agent) and 2.0 or 1.5
    end
    if agent.ShouldUseStandingFire ~= nil and agent:ShouldUseStandingFire() then
        return 0.0
    end
    if agent.IsInStandingCombatSlot ~= nil and agent:IsInStandingCombatSlot() then
        return 0.0
    end
    local inCover = agent.IsInCover ~= nil and agent:IsInCover()
    local engaging = agent.IsEngaging ~= nil and agent:IsEngaging()
    if inCover or engaging then
        return 1.0
    end
    return 0.0
end

function BeginPlay()
    animInstance = find_anim_instance()
    combatAgent = find_combat_agent()
    fireElapsed = 0.0
    lastMoveState = -1.0
    lastDeath = false
    lastEngaging = false
    hitBoolPulseTime = 0.0
    healthBillboards = collect_health_billboards()
    lastHealthIndicatorState = ""
    update_health_indicator(combatAgent)
    set_initial_variables()
end

function Tick(dt)
    local anim = get_anim_instance()
    if anim == nil then
        return
    end

    local agent = get_combat_agent()
    update_health_indicator(agent)
    local isDead = not agent_is_alive(agent)
    local isEngaging = agent ~= nil and agent.IsEngaging ~= nil and agent:IsEngaging()
    local hitTriggered = false
    if agent ~= nil and not isDead and agent.ConsumeHitReaction ~= nil then
        hitTriggered = agent:ConsumeHitReaction()
        if hitTriggered then
            fireElapsed = 0.0
            hitBoolPulseTime = HIT_BOOL_PULSE_DURATION
            set_graph_trigger(anim, "Hit")
        end
    end

    local bHitActive = hitTriggered or hitBoolPulseTime > 0.0
    local moveState = current_move_state(agent)

    anim:SetGraphVariableFloat("MoveState", moveState)
    anim:SetGraphVariableBool("Death", isDead)
    if anim.SetGraphVariableBool ~= nil then
        if isDead then
            hitBoolPulseTime = 0.0
            anim:SetGraphVariableBool("Hit", false)
        elseif hitBoolPulseTime > 0.0 then
            hitBoolPulseTime = hitBoolPulseTime - dt
            anim:SetGraphVariableBool("Hit", true)
        else
            hitBoolPulseTime = 0.0
            anim:SetGraphVariableBool("Hit", false)
        end
    end

    if moveState ~= lastMoveState or isDead ~= lastDeath then
        fireElapsed = 0.0
        lastMoveState = moveState
        lastDeath = isDead
    end

    if isEngaging and not lastEngaging then
        fireElapsed = FIRE_TRIGGER_INTERVAL
    end
    lastEngaging = isEngaging

    if isDead or bHitActive then
        return
    end

    if isEngaging then
        fireElapsed = fireElapsed + dt
        while fireElapsed >= FIRE_TRIGGER_INTERVAL do
            fireElapsed = fireElapsed - FIRE_TRIGGER_INTERVAL
            set_graph_trigger(anim, "Fire")
        end
    else
        fireElapsed = 0.0
    end
end

function OnSniperDamaged(hitInfo)
    publish_sniper_event("sniper.target_damaged", hitInfo)
end

function OnSniperKilled(hitInfo)
    publish_sniper_event("sniper.target_killed", hitInfo)
end
