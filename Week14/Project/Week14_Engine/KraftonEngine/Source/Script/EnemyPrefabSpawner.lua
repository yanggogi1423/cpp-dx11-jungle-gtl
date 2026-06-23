-- EnemyPrefabSpawner.lua
-- 이 스크립트를 가진 액터 위치에 지정 프리팹을 순차 스폰한다.
-- InGame 시작 후 정해진 시간마다 3종 적 프리팹을 1초 간격으로 웨이브 스폰한다.

local Config = {
    PrefabDirectory = "Content/Prefab",

    -- 같은 묶음 안에서 한 마리씩 뽑는 간격.
    SpawnIntervalSeconds = 1.0,

    -- 게임 시작 후 경과 시간(초) 기준 웨이브. 각 스포너가 이 수량만큼 따로 생성한다.
    WaveSchedule = {
        {
            TimeSeconds = 0.0,
            Enemies = {
                { PrefabName = "EnemyShortRange", Count = 3 },
                { PrefabName = "EnemyLongRangeSlow", Count = 1 },
                { PrefabName = "EnemyAssault", Count = 1 }
            }
        },
        {
            TimeSeconds = 60.0,
            Enemies = {
                { PrefabName = "EnemyShortRange", Count = 6 },
                { PrefabName = "EnemyLongRangeSlow", Count = 1 },
                { PrefabName = "EnemyAssault", Count = 1 }
            }
        },
        {
            TimeSeconds = 120.0,
            Enemies = {
                -- GPU skinning 적용 후 후반부 물량을 다시 늘리되 돌격형 비중은 높게 유지한다.
                { PrefabName = "EnemyShortRange", Count = 5 },
                { PrefabName = "EnemyLongRangeSlow", Count = 1 },
                { PrefabName = "EnemyAssault", Count = 4 }
            }
        },
        {
            TimeSeconds = 180.0,
            Enemies = {
                { PrefabName = "EnemyShortRange", Count = 6 },
                { PrefabName = "EnemyLongRangeSlow", Count = 2 },
                { PrefabName = "EnemyAssault", Count = 5 }
            }
        },
        {
            TimeSeconds = 240.0,
            Enemies = {
                { PrefabName = "EnemyShortRange", Count = 8 },
                { PrefabName = "EnemyLongRangeSlow", Count = 3 },
                { PrefabName = "EnemyAssault", Count = 7 }
            }
        }
    },

    -- InGame 상태 / ingame.started 이벤트와 연동해서 시작한다.
    StartOnInGame = true,

    -- true면 스폰 액터에 스포너의 회전을 적용한다.
    UseSpawnerRotation = true,

    -- 보통 프리팹의 스케일을 유지하는 게 안전하므로 기본 false.
    UseSpawnerScale = false,

    -- 스포너 위치 기준 추가 오프셋.
    SpawnOffset = Vec3(0.0, 0.0, 0.0),

    -- 스폰된 액터에 붙일 태그. 빈 문자열이면 붙이지 않는다.
    SpawnedTag = "SpawnedEnemy"
}

local subscribed = false
local subscriptionTokens = {}
local requestedStart = false
local running = false
local batchActive = false
local batchIndex = 0
local currentBatchQueue = {}
local spawnedInBatch = 0
local timeUntilNextSpawn = 0.0
local elapsedSinceStart = 0.0
local spawnedActors = {}

local function log(message)
    print("[EnemyPrefabSpawner] " .. tostring(message))
end

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid ~= nil then
        return actor:IsValid() == true
    end
    return true
end

local function get_actor_vec_property(actor, propertyName, fallback)
    if actor == nil then
        return fallback
    end

    local ok, value = pcall(function()
        return actor[propertyName]
    end)
    if ok and value ~= nil then
        return value
    end
    return fallback
end

local function get_spawn_location()
    return get_actor_vec_property(obj, "Location", Vec3(0.0, 0.0, 0.0)) + (Config.SpawnOffset or Vec3(0.0, 0.0, 0.0))
end

local function get_spawn_rotation()
    if Config.UseSpawnerRotation ~= true then
        return Vec3(0.0, 0.0, 0.0)
    end
    return get_actor_vec_property(obj, "Rotation", Vec3(0.0, 0.0, 0.0))
end

local function get_spawn_scale_or_nil()
    if Config.UseSpawnerScale ~= true then
        return nil
    end
    return get_actor_vec_property(obj, "Scale", Vec3(1.0, 1.0, 1.0))
end

local function spawn_actor_from_config(prefabName)
    if World == nil then
        log("World binding is nil")
        return nil
    end

    local location = get_spawn_location()
    local rotation = get_spawn_rotation()
    local scale = get_spawn_scale_or_nil()
    local actor = nil
    prefabName = tostring(prefabName or "")
    if prefabName == "" then
        log("empty prefab name")
        return nil
    end

    if World.SpawnActorFromPrefabByName ~= nil then
        if scale ~= nil then
            actor = World.SpawnActorFromPrefabByName(Config.PrefabDirectory, prefabName, location, rotation, scale)
        else
            actor = World.SpawnActorFromPrefabByName(Config.PrefabDirectory, prefabName, location, rotation)
        end
    elseif World.SpawnActorFromPrefabAt ~= nil then
        local path = tostring(Config.PrefabDirectory) .. "/" .. prefabName
        if scale ~= nil then
            actor = World.SpawnActorFromPrefabAt(path, location, rotation, scale)
        else
            actor = World.SpawnActorFromPrefabAt(path, location, rotation)
        end
    elseif World.SpawnActorFromPrefab ~= nil then
        local path = tostring(Config.PrefabDirectory) .. "/" .. prefabName
        actor = World.SpawnActorFromPrefab(path)
        if is_valid_actor(actor) then
            actor.Location = location
            actor.Rotation = rotation
            if scale ~= nil then
                actor.Scale = scale
            end
        end
    else
        log("no prefab spawn binding")
        return nil
    end

    if is_valid_actor(actor) then
        if Config.SpawnedTag ~= nil and Config.SpawnedTag ~= "" and actor.AddTag ~= nil then
            actor:AddTag(Config.SpawnedTag)
        end
        table.insert(spawnedActors, actor)
        log("spawned " .. prefabName .. " count=" .. tostring(#spawnedActors))
        return actor
    end

    log("failed to spawn prefab: " .. tostring(Config.PrefabDirectory) .. "/" .. prefabName)
    return nil
end

local function build_wave_queue(wave)
    local queue = {}
    if wave == nil or wave.Enemies == nil then
        return queue
    end

    for _, entry in ipairs(wave.Enemies) do
        local prefabName = tostring(entry.PrefabName or "")
        local count = math.max(0, math.floor(tonumber(entry.Count) or 0))
        for _ = 1, count do
            table.insert(queue, prefabName)
        end
    end
    return queue
end

local function can_start_new_batch()
    local schedule = Config.WaveSchedule or {}
    return batchIndex < #schedule
end

local function start_batch()
    local schedule = Config.WaveSchedule or {}
    local wave = schedule[batchIndex + 1]
    if wave == nil then
        running = false
        batchActive = false
        log("completed all waves")
        return
    end

    currentBatchQueue = build_wave_queue(wave)
    batchActive = true
    spawnedInBatch = 0
    timeUntilNextSpawn = 0.0
    log("wave start " .. tostring(batchIndex + 1) .. " count=" .. tostring(#currentBatchQueue))
end

local function request_start(reason)
    if requestedStart or running then
        return
    end

    requestedStart = true
    running = true
    batchActive = false
    batchIndex = 0
    currentBatchQueue = {}
    spawnedInBatch = 0
    timeUntilNextSpawn = 0.0
    elapsedSinceStart = 0.0
    log("start reason=" .. tostring(reason))
end

local function is_ingame_state()
    if GameGeneralManager == nil or GameGeneralManager.GetState == nil then
        return false
    end

    local ok, state = pcall(function()
        return GameGeneralManager:GetState()
    end)
    return ok and state == "InGame"
end

local function subscribe_game_events()
    if subscribed or GameGeneralManager == nil or GameGeneralManager.Subscribe == nil then
        return
    end

    subscribed = true
    local owner = obj or this

    local tokenStarted = GameGeneralManager:Subscribe("ingame.started", owner, function(payload)
        request_start("ingame.started")
    end)
    table.insert(subscriptionTokens, tokenStarted)

    local tokenSceneEntered = GameGeneralManager:Subscribe("scene.entered", owner, function(payload)
        if payload ~= nil and payload.to == "InGame" then
            request_start("scene.entered")
        end
    end)
    table.insert(subscriptionTokens, tokenSceneEntered)
end

local function unsubscribe_game_events()
    if GameGeneralManager ~= nil and GameGeneralManager.Unsubscribe ~= nil then
        for _, token in ipairs(subscriptionTokens) do
            GameGeneralManager:Unsubscribe(token)
        end
    end
    subscriptionTokens = {}
    subscribed = false
end

function BeginPlay()
    subscribed = false
    subscriptionTokens = {}
    requestedStart = false
    running = false
    batchActive = false
    batchIndex = 0
    currentBatchQueue = {}
    spawnedInBatch = 0
    timeUntilNextSpawn = 0.0
    elapsedSinceStart = 0.0
    spawnedActors = {}

    subscribe_game_events()
    if Config.StartOnInGame ~= true then
        request_start("beginplay")
    elseif is_ingame_state() then
        request_start("beginplay_ingame")
    end
end

function EndPlay()
    unsubscribe_game_events()
    running = false
    batchActive = false
end

function Tick(dt)
    dt = tonumber(dt) or 0.0

    if not subscribed then
        subscribe_game_events()
    end

    if not running then
        if Config.StartOnInGame == true and is_ingame_state() then
            request_start("tick_ingame")
        end
        return
    end

    elapsedSinceStart = elapsedSinceStart + dt

    if not batchActive then
        if not can_start_new_batch() then
            running = false
            log("finished")
            return
        end

        local schedule = Config.WaveSchedule or {}
        local wave = schedule[batchIndex + 1]
        local waveTime = tonumber(wave and wave.TimeSeconds) or 0.0
        if elapsedSinceStart >= waveTime then
            start_batch()
        end
        return
    end

    if #currentBatchQueue <= 0 then
        batchActive = false
        batchIndex = batchIndex + 1
        return
    end

    timeUntilNextSpawn = timeUntilNextSpawn - dt
    if timeUntilNextSpawn <= 0.0 then
        local prefabName = currentBatchQueue[spawnedInBatch + 1]
        spawn_actor_from_config(prefabName)
        spawnedInBatch = spawnedInBatch + 1

        if spawnedInBatch >= #currentBatchQueue then
            batchActive = false
            batchIndex = batchIndex + 1
        else
            timeUntilNextSpawn = math.max(0.0, tonumber(Config.SpawnIntervalSeconds) or 0.0)
        end
    end
end
