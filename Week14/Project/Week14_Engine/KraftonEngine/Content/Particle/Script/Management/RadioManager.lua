local GameState = require("Management/GameState")

local RadioManager = {}
RadioManager.__index = RadioManager

local WALKIE_SFX = "SFX/Comment/Walkie-Talkie.mp3"
local WALKIE_DURATION = 0.55
local WALKIE_TO_VOICE_DELAY = 0.18
local VOICE_TO_WALKIE_DELAY = 0.12
local RADIO_VOLUME = 1.0

local OPENING_FADE_SECONDS = 3.0
local OPENING_SKIP_UNLOCK_SECONDS = 5.0
local OPENING_SKIP_PROMPT_FADE_SECONDS = 0.35

local COMMENT_DEFINITIONS = {
    Opening = {
        path = "SFX/Comment/Opening.wav",
        duration = 25.356,
        subtitle = nil,
        subtitle_segments = {
            {
                until_time = 7.0,
                text = "브라보 원, 여기는 오버로드. 현재 귀소대 주둔지인 코소보 B1 섹터 외곽에 다수의 적 침투가 식별되었다."
            },
            {
                until_time = 13.0,
                text = "현재 가용 가능한 공중지원의 도착 예정 시간은 5분이다. 반복한다, 지원까지 앞으로 5분."
            },
            {
                until_time = 19.0,
                text = "저격수, 고지에서 분대원들의 시야를 확보하고 접근하는 적들을 차단하라. 아군을 생존시켜야 한다."
            },
            {
                until_time = 25.356,
                text = "방어선이 뚫려 분대 전멸 시, 전선 전체가 무너진다. 반드시 사수하라. 이상."
            }
        },
        opening = true
    },
    Victory = {
        path = "SFX/Comment/Ending/Victory.wav",
        duration = 3.622,
        subtitle = "건십 전장 진입 완료. 전 분대 고개 숙여라. 빗자루질을 시작한다."
    },
    Defeat1 = {
        path = "SFX/Comment/Ending/Defeat-1.wav",
        duration = 2.322,
        subtitle = "방어선이 완전히... 뚫렸... 으윽..."
    },
    Defeat2 = {
        path = "SFX/Comment/Ending/Defeat-2.wav",
        duration = 4.830,
        subtitle = "전선이 완전히 붕괴되었다. 작전 실패. 지원 세력을 전 전장에서 회항시킨다."
    },
    TangoDown = {
        path = "SFX/Comment/Sniper/Tango-Down.wav",
        duration = 0.975,
        subtitle = nil
    },
    TangoDownNice = {
        path = "SFX/Comment/Sniper/Tango-Down-Nice.wav",
        duration = 1.997,
        subtitle = nil
    },
    Mistake1 = {
        path = "SFX/Mistake/Mistake1.wav",
        duration = 3.483,
        subtitle = "야 이 미친 새끼야! 지금 어딜 쏘는 거야?! 아군이라고!"
    },
    Mistake2 = {
        path = "SFX/Mistake/Mistake2.wav",
        duration = 3.715,
        subtitle = "젠장, 저격수 새끼 눈이 삐었나! 우리 편을 쏘면 어쩌자는 거야!"
    },
    Mistake3 = {
        path = "SFX/Mistake/Mistake3.wav",
        duration = 4.644,
        subtitle = "**, 네가 쐈잖아! 브라보 투 대가리가 날아갔다고, 이 ***야!"
    },
    Mistake4 = {
        path = "SFX/Mistake/Mistake4.wav",
        duration = 6.594,
        subtitle = "야 이 ****야, 네가 쐈잖아! 본부, 저격수 저 새끼 미쳤으니까 당장 죽여버려!"
    },
    SquadClearFront = {
        path = "SFX/Comment/Squad/Clear-front.wav",
        duration = 1.579,
        subtitle = "저격수, 내 전방의 적들 좀 치워줘!"
    },
    SquadCritical = {
        path = "SFX/Comment/Squad/Critical.wav",
        duration = 1.997,
        subtitle = "브라보 쓰리 치명상! 피가 너무 많이 난다!"
    },
    SquadHelpMe = {
        path = "SFX/Comment/Squad/Help-me.wav",
        duration = 1.858,
        subtitle = "저격수, 나 좀 살려줘! 엄호해!"
    },
    SquadLastMan = {
        path = "SFX/Comment/Squad/Last-man.wav",
        duration = 2.694,
        subtitle = "다 죽었어, 나 혼자 남았다고!"
    },
    SquadOverwatch = {
        path = "SFX/Comment/Squad/Overwatch.wav",
        duration = 2.972,
        subtitle = "강한 압박을 받고 있다! 저격수! 엄호 사격 바람!"
    },
    Time3Min1 = {
        path = "SFX/Comment/Time/3min-1.wav",
        duration = 5.248,
        subtitle = "브라보, 지원 세력이 현재 대공망 때문에 지체되고 있다. 앞으로 3분 더 버텨야 한다."
    },
    Time3Min2 = {
        path = "SFX/Comment/Time/3min-2.wav",
        duration = 5.155,
        subtitle = "3분이나 더? 본부, 장난해? 탄약도 바닥나 가는데 끝도 없이 밀려온다고!"
    },
    Time60Sec1 = {
        path = "SFX/Comment/Time/60sec-1.wav",
        duration = 4.783,
        subtitle = "전 분대 주목, 공중지원 전장 진입 60초 전이다. 버텨라, 다 왔어."
    },
    Time60Sec2 = {
        path = "SFX/Comment/Time/60sec-2.wav",
        duration = 4.969,
        subtitle = "마지막 탄창이다! 저격수, 저기 오는 새끼들 대가리 다 날려버려! 1분만 버티자!"
    },
    Time30Sec = {
        path = "SFX/Comment/Time/30sec.wav",
        duration = 3.529,
        subtitle = "브라보, 지원군이 눈앞에 있다. 방어선 유지해라, 앞으로 30초!"
    }
}

COMMENT_DEFINITIONS.Victory.subtitle = "건십 전장 진입 완료. 전 분대 고개 숙여라(Danger Close). 빗자루질을 시작한다."
COMMENT_DEFINITIONS.Defeat1.subtitle = "방어선이 완전히... 뚫렸... 으윽..."
COMMENT_DEFINITIONS.Defeat2.subtitle = "전선이 완전히 붕괴되었다. 작전 실패. 지원 세력을 전 전장에서 회항시킨다."

local TIME_SEQUENCE = {
    { remaining = 180, comments = { "Time3Min1", "Time3Min2" } },
    { remaining = 60, comments = { "Time60Sec1", "Time60Sec2" } },
    { remaining = 30, comments = { "Time30Sec" } }
}

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[RadioManager] " .. message)
    else
        print("[RadioManager] " .. message)
    end
end

local function raw_delta_time(fallback)
    if Time ~= nil and Time.RawDeltaTime ~= nil then
        local raw = tonumber(Time.RawDeltaTime()) or 0.0
        if raw > 0.0 then
            return raw
        end
    end
    return fallback or 0.0
end

local function get_time_dilation()
    if Time ~= nil and Time.GetTimeDilation ~= nil then
        return Time.GetTimeDilation()
    end
    return 1.0
end

local function set_time_dilation(value)
    if Time ~= nil and Time.SetTimeDilation ~= nil then
        Time.SetTimeDilation(value)
    end
end

local function get_input_mode()
    if Input ~= nil and Input.GetInputMode ~= nil then
        return Input.GetInputMode()
    end
    return nil
end

local function set_input_mode(mode)
    if Input == nil then
        return
    end
    if mode == "UIOnly" and Input.SetInputModeUIOnly ~= nil then
        Input.SetInputModeUIOnly()
    elseif mode == "GameAndUI" and Input.SetInputModeGameAndUI ~= nil then
        Input.SetInputModeGameAndUI()
    elseif mode == "GameOnly" and Input.SetInputModeGameOnly ~= nil then
        Input.SetInputModeGameOnly()
    elseif Input.SetInputMode ~= nil then
        Input.SetInputMode(mode)
    end
end

local function get_mouse_captured()
    if Input ~= nil and Input.IsMouseCaptured ~= nil then
        return Input.IsMouseCaptured() == true
    end
    return nil
end

local function set_mouse_captured(captured)
    if Input == nil then
        return
    end
    if captured == true then
        if Input.SetMouseCaptured ~= nil then
            Input.SetMouseCaptured(true)
        end
    elseif Input.ReleaseMouseCapture ~= nil then
        Input.ReleaseMouseCapture()
    elseif Input.SetMouseCaptured ~= nil then
        Input.SetMouseCaptured(false)
    end
end

local function random_index(count)
    if count <= 1 then
        return 1
    end
    if Random ~= nil and Random.RandomInt ~= nil then
        return Random.RandomInt(1, count)
    end
    return math.random(1, count)
end

local function read_method(target, method_name)
    if target == nil or target[method_name] == nil then
        return nil
    end

    local ok, value = pcall(function()
        return target[method_name](target)
    end)
    if ok then
        return value
    end
    return nil
end

local function read_number_method(target, method_names)
    for _, method_name in ipairs(method_names) do
        local value = tonumber(read_method(target, method_name))
        if value ~= nil then
            return value
        end
    end
    return nil
end

local function read_string_method(target, method_names)
    for _, method_name in ipairs(method_names) do
        local value = read_method(target, method_name)
        if value ~= nil then
            local text = tostring(value)
            if text ~= "" then
                return text
            end
        end
    end
    return nil
end

local function is_alive_agent(agent)
    if agent == nil then
        return false
    end
    if agent.IsAlive == nil then
        return true
    end
    return read_method(agent, "IsAlive") == true
end

local function is_friendly_team(team_tag)
    if team_tag == nil or team_tag == "" then
        return false
    end

    local team = string.lower(tostring(team_tag))
    return string.find(team, "ally", 1, true) ~= nil
        or string.find(team, "friendly", 1, true) ~= nil
        or string.find(team, "player", 1, true) ~= nil
end

local function format_radio_subtitle(text)
    text = tostring(text or "")
    text = string.gsub(text, "^%s*본부%s*%(Overlord%)%s*:%s*", "")
    text = string.gsub(text, "^%s*본부%s*%(Overload%)%s*:%s*", "")
    return text
end

local function clone_definition(id, overrides)
    local base = COMMENT_DEFINITIONS[id]
    if base == nil then
        return nil
    end

    local result = {}
    for key, value in pairs(base) do
        result[key] = value
    end
    result.id = id

    if type(overrides) == "table" then
        for key, value in pairs(overrides) do
            result[key] = value
        end
    end

    result.duration = tonumber(result.duration) or 0.0
    return result
end

function RadioManager.new(general)
    return setmetatable({
        general = general,
        queue = {},
        current = nil,
        cutscene_active = false,
        played_opening_comment = false,
        played_result_comment = false,
        fired_time_markers = {},
        squad_warning_by_id = {},
        squad_comment_cooldown = 0.0,
        last_ally_alive_count = nil,
        last_man_queued = false,
        previous_time_dilation = nil,
        previous_input_mode = nil,
        previous_mouse_captured = nil,
        opening_input_override_active = false,
        opening_time_restored = false,
        opening_skip_key_down = false,
        opening_exit_fade_active = false,
        opening_exit_fade_elapsed = 0.0,
        active_sfx_handles = {}
    }, RadioManager)
end

function RadioManager:Initialize()
    self.general:Subscribe("cutscene.started", self, function()
        self.cutscene_active = true
    end)

    self.general:Subscribe("cutscene.stopped", self, function()
        self.cutscene_active = false
        self:TryStartNext()
    end)

    self.general:Subscribe("scene.entered", self, function(payload)
        if payload ~= nil and payload.to == GameState.InGame then
            self:ResetRunState()
        elseif payload ~= nil and payload.to == GameState.Victory then
            self:QueueEnding("Victory")
        elseif payload ~= nil and payload.to == GameState.Defeat1 then
            self:QueueEnding("Defeat")
        elseif payload ~= nil and payload.to == GameState.Defeat2 then
            self:QueueEnding("Defeat")
        end
    end)

    self.general:Subscribe("ingame.started", self, function()
        self:ResetRunState()
        self.played_opening_comment = true
        self:QueueOpening()
    end)

    self.general:Subscribe("ingame.timer", self, function(payload)
        self:HandleTimer(payload)
    end)

    self.general:Subscribe("ingame.debug_time_warp", self, function(payload)
        self:HandleDebugTimeWarp(payload)
    end)

    self.general:Subscribe("ingame.sniper_killed", self, function(payload)
        self:HandleSniperKilled(payload)
    end)

    self.general:Subscribe("ingame.completed", self, function(payload)
        if payload ~= nil then
            self:QueueEnding(payload.result)
        end
    end)

    self.general:Subscribe("radio.queue", self, function(payload)
        if payload ~= nil then
            self:Queue(payload.id, payload)
        end
    end)
end

function RadioManager:Shutdown()
    self:StopActiveRadioSFX()
    self:ClearPresentation()
    self:RestoreOpeningTime()
    self:RestoreOpeningInputMode()
    self.queue = {}
    self.current = nil
    self.general:UnsubscribeOwner(self)
end

function RadioManager:ResetRunState()
    self.played_result_comment = false
    self.played_opening_comment = false
    self.fired_time_markers = {}
    self.squad_warning_by_id = {}
    self.squad_comment_cooldown = 0.0
    self.last_ally_alive_count = nil
    self.last_man_queued = false
    self.opening_skip_key_down = false
    self.opening_exit_fade_active = false
    self.opening_exit_fade_elapsed = 0.0
    self:StopActiveRadioSFX()
    self.queue = {}
    self.current = nil
    self:ClearPresentation()
    self:RestoreOpeningTime()
    self:RestoreOpeningInputMode()
end

function RadioManager:IsBlocked()
    if self.cutscene_active then
        return true
    end

    local cutscene = self.general and self.general.managers and self.general.managers.CutScene or nil
    return cutscene ~= nil and cutscene.current ~= nil
end

function RadioManager:Queue(id, overrides)
    local comment = clone_definition(id, overrides)
    if comment == nil then
        log("missing comment id=" .. tostring(id))
        return false
    end

    self.queue[#self.queue + 1] = comment
    return true
end

function RadioManager:QueueOpening(overrides)
    return self:Queue("Opening", overrides)
end

function RadioManager:QueueEnding(result)
    if self.played_result_comment then
        return false
    end

    local result_text = tostring(result or "")
    if result_text == "Victory" then
        self.played_result_comment = true
        return self:Queue("Victory")
    end

    if result_text == "Defeat" or result_text == "Defeat1" or result_text == "Defeat2" then
        self.played_result_comment = true
        local id = result_text
        if result_text == "Defeat" then
            id = random_index(2) == 1 and "Defeat1" or "Defeat2"
        end
        return self:Queue(id)
    end

    return false
end

function RadioManager:HandleSniperKilled(payload)
    if payload ~= nil and payload.friendly == true then
        local id = "Mistake" .. tostring(random_index(4))
        self:Queue(id)
        return
    end

    local id = random_index(2) == 1 and "TangoDown" or "TangoDownNice"
    self:Queue(id)
end

function RadioManager:HandleTimer(payload)
    if payload == nil then
        return
    end

    local remaining = tonumber(payload.remaining_time)
    if remaining == nil and payload.match_duration ~= nil and payload.timer ~= nil then
        remaining = (tonumber(payload.match_duration) or 0.0) - (tonumber(payload.timer) or 0.0)
    end
    if remaining == nil then
        return
    end

    for _, marker in ipairs(TIME_SEQUENCE) do
        if self.fired_time_markers[marker.remaining] ~= true and remaining <= marker.remaining then
            self.fired_time_markers[marker.remaining] = true
            for _, id in ipairs(marker.comments) do
                self:Queue(id)
            end
        end
    end
end

function RadioManager:HandleDebugTimeWarp(payload)
    if payload == nil then
        return
    end

    local remaining = tonumber(payload.remaining_time)
    if remaining == nil and payload.match_duration ~= nil and payload.timer ~= nil then
        remaining = (tonumber(payload.match_duration) or 0.0) - (tonumber(payload.timer) or 0.0)
    end
    if remaining == nil then
        return
    end

    for _, marker in ipairs(TIME_SEQUENCE) do
        if remaining > marker.remaining then
            self.fired_time_markers[marker.remaining] = nil
        end
    end
end

function RadioManager:PollSquadState(dt)
    if self.general == nil or self.general.GetState == nil or self.general:GetState() ~= GameState.InGame then
        return
    end
    if Combat == nil or Combat.GetAgents == nil then
        return
    end

    self.squad_comment_cooldown = math.max(0.0, (self.squad_comment_cooldown or 0.0) - (dt or 0.0))

    local ok, agents = pcall(function()
        return Combat.GetAgents()
    end)
    if not ok or agents == nil then
        return
    end

    local allies = {}
    for _, agent in ipairs(agents) do
        if is_alive_agent(agent) and is_friendly_team(read_string_method(agent, { "GetTeamTag" }) or "") then
            allies[#allies + 1] = agent
        end
    end

    if self.last_ally_alive_count ~= nil and
        self.last_ally_alive_count > 1 and
        #allies == 1 and
        self.last_man_queued ~= true then
        self.last_man_queued = true
        self:Queue("SquadLastMan")
        self.squad_comment_cooldown = 8.0
    end
    self.last_ally_alive_count = #allies

    if self.squad_comment_cooldown > 0.0 then
        return
    end

    for _, agent in ipairs(allies) do
        local health = read_number_method(agent, { "GetHealth" }) or 0.0
        local max_health = read_number_method(agent, { "GetMaxHealth" }) or 0.0
        local ratio = read_number_method(agent, { "GetHealthRatio" })
        if ratio == nil then
            ratio = max_health > 0.0 and health / max_health or 1.0
        end

        local id = read_string_method(agent, { "GetDisplayName", "GetName" }) or tostring(agent)
        if ratio <= 0.35 and self.squad_warning_by_id[id] ~= true then
            self.squad_warning_by_id[id] = true
            local comments = { "SquadCritical", "SquadHelpMe", "SquadOverwatch", "SquadClearFront" }
            self:Queue(comments[random_index(#comments)])
            self.squad_comment_cooldown = 8.0
            return
        end
    end
end

function RadioManager:TryStartNext()
    if self.current ~= nil or self:IsBlocked() then
        return
    end

    local next_comment = table.remove(self.queue, 1)
    if next_comment == nil then
        return
    end

    self:StartComment(next_comment)
end

function RadioManager:StartComment(comment)
    self.active_sfx_handles = {}
    local voice_start = WALKIE_DURATION + WALKIE_TO_VOICE_DELAY
    local voice_end = voice_start + math.max(0.0, tonumber(comment.duration) or 0.0)
    local outro_start = voice_end + VOICE_TO_WALKIE_DELAY
    local total = outro_start + WALKIE_DURATION
    if comment.opening == true then
        total = math.max(total, voice_end + OPENING_FADE_SECONDS)
        self.previous_time_dilation = get_time_dilation()
        self.previous_input_mode = get_input_mode()
        self.previous_mouse_captured = get_mouse_captured()
        self.opening_input_override_active = true
        self.opening_time_restored = false
        set_input_mode("UIOnly")
        set_mouse_captured(false)
        set_time_dilation(0.0)
        self.opening_exit_fade_active = false
        self.opening_exit_fade_elapsed = 0.0
        self:PublishOpeningPresentation(1.0, true, true, 0.0)
    end

    self.current = {
        comment = comment,
        elapsed = 0.0,
        voice_start = voice_start,
        voice_end = voice_end,
        outro_start = outro_start,
        total = total,
        voice_played = false,
        subtitle_shown = false,
        active_subtitle_index = 0,
        subtitle_hidden = false,
        outro_played = false
    }

    self:PlaySFX(WALKIE_SFX, RADIO_VOLUME)
end

function RadioManager:Tick(dt)
    local step = raw_delta_time(dt or 0.0)
    self:PollSquadState(step)
    if self.current == nil then
        if self:TickOpeningExitFade(step) then
            return
        end
        self:TryStartNext()
        return
    end

    local current = self.current
    current.elapsed = current.elapsed + step
    if self:ConsumeOpeningSkip(current) then
        return
    end
    self:TickOpeningPresentation(current)

    if current.voice_played ~= true and current.elapsed >= current.voice_start then
        current.voice_played = true
        self:PlaySFX(current.comment.path, current.comment.volume or RADIO_VOLUME)
        if current.comment.subtitle ~= nil and current.comment.subtitle ~= "" then
            current.subtitle_shown = true
            self:PublishSubtitle(current.comment.subtitle, true)
        end
    end

    self:TickSegmentSubtitle(current)

    if current.subtitle_hidden ~= true and current.elapsed >= current.voice_end then
        current.subtitle_hidden = true
        self:PublishSubtitle("", false)
    end

    if current.outro_played ~= true and current.elapsed >= current.outro_start then
        current.outro_played = true
        self:PlaySFX(WALKIE_SFX, RADIO_VOLUME)
    end

    if current.elapsed >= current.total then
        self:FinishCurrent()
    end
end

function RadioManager:ConsumeOpeningSkip(current)
    if current == nil or current.comment == nil or current.comment.opening ~= true then
        self.opening_skip_key_down = false
        return false
    end
    if current.elapsed < OPENING_SKIP_UNLOCK_SECONDS then
        self.opening_skip_key_down = false
        return false
    end
    if Input == nil then
        return false
    end

    local should_skip = false
    if Input.WasConfirmPressed ~= nil then
        should_skip = Input.WasConfirmPressed() == true
    end
    if not should_skip and Input.GetRawKeyDown ~= nil then
        should_skip = Input.GetRawKeyDown("Space") == true or Input.GetRawKeyDown("SpaceBar") == true
    end
    if not should_skip and Input.GetKeyDown ~= nil then
        should_skip = Input.GetKeyDown("Space") == true or Input.GetKeyDown("SpaceBar") == true
    end
    local space_down = false
    if Input.GetRawKey ~= nil then
        space_down = Input.GetRawKey("Space") == true or Input.GetRawKey("SpaceBar") == true
    elseif Input.GetKey ~= nil then
        space_down = Input.GetKey("Space") == true or Input.GetKey("SpaceBar") == true
    end
    if not should_skip then
        should_skip = space_down and self.opening_skip_key_down ~= true
    end

    self.opening_skip_key_down = space_down
    if not should_skip then
        return false
    end

    self:FinishCurrent(true)
    return true
end

function RadioManager:TickSegmentSubtitle(current)
    if current == nil or current.comment == nil or type(current.comment.subtitle_segments) ~= "table" then
        return
    end
    if current.voice_played ~= true or current.elapsed < current.voice_start or current.elapsed >= current.voice_end then
        return
    end

    local voice_time = current.elapsed - current.voice_start
    local selected_index = 0
    local selected_text = nil
    for index, segment in ipairs(current.comment.subtitle_segments) do
        if voice_time <= (tonumber(segment.until_time) or 0.0) then
            selected_index = index
            selected_text = segment.text
            break
        end
    end

    if selected_index > 0 and selected_index ~= current.active_subtitle_index then
        current.active_subtitle_index = selected_index
        self:PublishSubtitle(selected_text or "", selected_text ~= nil and selected_text ~= "")
    end
end

function RadioManager:TickOpeningPresentation(current)
    if current.comment.opening ~= true then
        return
    end

    local elapsed = tonumber(current.elapsed) or 0.0
    local skip_alpha = 0.0
    if elapsed >= OPENING_SKIP_UNLOCK_SECONDS then
        skip_alpha = math.min(1.0, (elapsed - OPENING_SKIP_UNLOCK_SECONDS) / OPENING_SKIP_PROMPT_FADE_SECONDS)
    end

    local fade_start = math.max(0.0, (tonumber(current.total) or 0.0) - OPENING_FADE_SECONDS)
    if elapsed < fade_start then
        self:PublishOpeningPresentation(1.0, true, true, skip_alpha)
        return
    end

    local fade_alpha = 1.0 - math.min(1.0, (elapsed - fade_start) / OPENING_FADE_SECONDS)
    self:PublishOpeningPresentation(fade_alpha, fade_alpha > 0.001, false, skip_alpha)
end

function RadioManager:BeginOpeningExitFade()
    self.opening_exit_fade_active = true
    self.opening_exit_fade_elapsed = 0.0
    self:PublishOpeningPresentation(1.0, true, false, 0.0)
end

function RadioManager:TickOpeningExitFade(dt)
    if self.opening_exit_fade_active ~= true then
        return false
    end

    self.opening_exit_fade_elapsed = (self.opening_exit_fade_elapsed or 0.0) + math.max(0.0, tonumber(dt) or 0.0)
    local t = math.min(1.0, self.opening_exit_fade_elapsed / OPENING_FADE_SECONDS)
    local alpha = 1.0 - t
    self:PublishOpeningPresentation(alpha, alpha > 0.001, false, 0.0)
    if t >= 1.0 then
        self.opening_exit_fade_active = false
        self.opening_exit_fade_elapsed = 0.0
        self:PublishOpeningPresentation(0.0, false, false, 0.0)
    end
    return true
end

function RadioManager:FinishCurrent(stop_audio)
    local finished_comment = self.current ~= nil and self.current.comment or nil
    local finished_id = finished_comment ~= nil and finished_comment.id or nil
    local was_opening = finished_comment ~= nil and finished_comment.opening == true
    self.current = nil
    if stop_audio == true then
        self:StopActiveRadioSFX()
    else
        self.active_sfx_handles = {}
    end
    self:PublishSubtitle("", false)
    if was_opening then
        self:RestoreOpeningTime()
        self:RestoreOpeningInputMode()
        if stop_audio == true then
            self:BeginOpeningExitFade()
        else
            self.opening_exit_fade_active = false
            self.opening_exit_fade_elapsed = 0.0
            self:PublishOpeningPresentation(0.0, false, false, 0.0)
        end
    end
    if self.general ~= nil and self.general.Publish ~= nil and finished_id ~= nil then
        self.general:Publish("radio.comment_finished", {
            id = finished_id,
            comment = finished_comment,
            stopped = stop_audio == true
        })
    end
    if self.opening_exit_fade_active == true then
        return
    end
    self:TryStartNext()
end

function RadioManager:StopActiveRadioSFX()
    if type(self.active_sfx_handles) ~= "table" then
        self.active_sfx_handles = {}
        return
    end

    for _, handle in ipairs(self.active_sfx_handles) do
        if handle ~= nil and tonumber(handle) ~= nil and tonumber(handle) ~= 0 then
            if self.general ~= nil and self.general.FadeOutSound ~= nil then
                self.general:FadeOutSound(handle, 0.0)
            elseif self.general ~= nil and self.general.FadeOutSFX ~= nil then
                self.general:FadeOutSFX(handle, 0.0)
            end
        end
    end
    self.active_sfx_handles = {}
end

function RadioManager:RestoreOpeningTime()
    if self.previous_time_dilation ~= nil then
        set_time_dilation(self.previous_time_dilation)
        self.previous_time_dilation = nil
    end
    self.opening_time_restored = true
end

function RadioManager:RestoreOpeningInputMode()
    if self.opening_input_override_active ~= true then
        return
    end

    if self.previous_input_mode ~= nil then
        set_input_mode(self.previous_input_mode)
        self.previous_input_mode = nil
    else
        set_input_mode("GameOnly")
    end
    if self.previous_mouse_captured ~= nil then
        set_mouse_captured(self.previous_mouse_captured)
        self.previous_mouse_captured = nil
    end
    self.opening_input_override_active = false
end

function RadioManager:ClearPresentation()
    self.opening_exit_fade_active = false
    self.opening_exit_fade_elapsed = 0.0
    self:PublishSubtitle("", false)
    self:PublishOpeningPresentation(0.0, false, false, 0.0)
end

function RadioManager:PublishSubtitle(text, visible)
    text = format_radio_subtitle(text)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("radio.subtitle", {
            visible = visible == true,
            text = text
        })
    end
end

function RadioManager:PublishOpeningPresentation(alpha, blackout_visible, hud_suppressed, skip_prompt_alpha)
    if self.general ~= nil and self.general.Publish ~= nil then
        self.general:Publish("radio.opening_presentation", {
            active = blackout_visible == true or hud_suppressed == true,
            blackout_alpha = alpha or 0.0,
            hud_suppressed = hud_suppressed == true,
            skip_prompt_alpha = skip_prompt_alpha or 0.0
        })
    end
end

function RadioManager:PlaySFX(path, volume)
    local handle = 0
    if self.general ~= nil and self.general.PlaySFX ~= nil then
        if self.general.PlaySFXHandle ~= nil then
            handle = self.general:PlaySFXHandle(path, volume or RADIO_VOLUME) or 0
        else
            self.general:PlaySFX(path, volume or RADIO_VOLUME)
        end
    elseif AudioManager ~= nil and AudioManager.PlaySFXHandle ~= nil then
        handle = AudioManager.PlaySFXHandle(path, volume or RADIO_VOLUME) or 0
    elseif AudioManager ~= nil and AudioManager.PlaySFX ~= nil then
        AudioManager.PlaySFX(path, volume or RADIO_VOLUME)
    end

    if tonumber(handle) ~= nil and tonumber(handle) ~= 0 then
        self.active_sfx_handles[#self.active_sfx_handles + 1] = handle
    end
    return handle
end

return RadioManager
