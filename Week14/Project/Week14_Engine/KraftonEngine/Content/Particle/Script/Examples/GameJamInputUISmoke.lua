-- Attach this script to any actor in PIE to verify UI/game input routing.
-- Expected: clicking/typing in the panel updates UI without leaking the same
-- frame into game input. Press F1 to toggle the panel.

local widget = nil
local visible = false
local pulse_time = 0.0
local typed_total = ""

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[InputUISmoke] " .. message)
    else
        print("[InputUISmoke] " .. message)
    end
end

local function set_mode_for_panel()
    if Input and Input.SetInputModeGameAndUI then
        Input.SetInputModeGameAndUI()
    end
    if Input and Input.SetCursorVisible then
        Input.SetCursorVisible(true)
    end
    if Input and Input.ReleaseMouseCapture then
        Input.ReleaseMouseCapture()
    end
end

local function show_panel()
    if widget == nil and UI and UI.CreateWidget then
        widget = UI.CreateWidget("Content/UI/Examples/GameJamInputSmoke.rml")
        if widget ~= nil then
            widget:AddToViewport(100)
            widget:SetWantsMouse(true)
            widget:SetWantsKeyboard(true)
            widget:SetWantsTextInput(true)
            widget:SetBlocksGameInput(false)
            widget:SetBlocksGameKeyboard(true)
            widget:SetBlocksGameMouseLook(true)
            widget:SetActionEvent("pulse", "pulse")
            widget:SetActionEvent("close", "close")
            widget:SetTransitionAll("panel", 0.12, "ease-out", 0.0)
            widget:SetTransitionAll("status", 0.10, "linear", 0.0)
        end
    end

    if widget == nil then
        log("failed to create widget")
        return
    end

    visible = true
    set_mode_for_panel()
    widget:SetVisible("panel", true)
    widget:SetText("status", "Panel active. Click buttons or type in the input field.")
    widget:AnimateAlpha("panel", 1.0, 0.12, "ease-out", 0.0)
end

local function hide_panel()
    visible = false
    if widget ~= nil then
        widget:SetText("status", "Panel hidden.")
        widget:AnimateAlpha("panel", 0.0, 0.10, "ease-out", 0.0)
        widget:SetVisible("panel", false)
    end
    if Input and Input.SetInputModeGameOnly then
        Input.SetInputModeGameOnly()
    end
end

local function handle_actions()
    if widget == nil then
        return
    end

    local events = widget:PollActionEvents()
    for _, event_name in ipairs(events) do
        if event_name == "pulse" then
            pulse_time = 0.25
            widget:SetClass("panel", "pulse", true)
            widget:SetText("status", "Pulse clicked. Same-frame game click should be consumed.")
            log("pulse action")
        elseif event_name == "close" then
            hide_panel()
            log("close action")
        end
    end
end

local function handle_text()
    if not visible or not Input or not Input.ConsumeTextInput then
        return
    end

    local text = Input.ConsumeTextInput()
    if text ~= nil and text ~= "" then
        typed_total = typed_total .. text
        if widget ~= nil then
            widget:SetText("status", "Lua text queue saw: " .. typed_total)
        end
        log("text=" .. text)
    end
end

function BeginPlay()
    log("BeginPlay " .. obj.UUID)
    show_panel()
end

function EndPlay()
    if widget ~= nil then
        widget:RemoveFromParent()
        widget = nil
    end
    if Input and Input.SetInputModeGameOnly then
        Input.SetInputModeGameOnly()
    end
end

function Tick(dt)
    if Input and Input.GetKeyDown and Input.GetKeyDown("F1") then
        if visible then
            hide_panel()
        else
            show_panel()
        end
    end

    handle_actions()
    handle_text()

    if pulse_time > 0.0 then
        pulse_time = pulse_time - dt
        if pulse_time <= 0.0 and widget ~= nil then
            widget:SetClass("panel", "pulse", false)
        end
    end
end
