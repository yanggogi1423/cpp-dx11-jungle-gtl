-- Attach this script to an actor that has an ActorSequenceComponent.
-- Expected: F2 runs the sequence, F3 pauses, F4 stops/restores base values.

local seq_comp = nil

local function log(message)
    if Debug and Debug.Log then
        Debug.Log("[ActorSequenceSmoke] " .. message)
    else
        print("[ActorSequenceSmoke] " .. message)
    end
end

local function find_sequence_component()
    if obj == nil or obj.GetActorSequenceComponent == nil then
        return nil
    end
    return obj:GetActorSequenceComponent()
end

local function run_round_trip_self_test()
    if Debug and Debug.RunActorSequenceRoundTripSelfTest then
        local result = Debug.RunActorSequenceRoundTripSelfTest()
        if result ~= nil then
            log("diagnostics passed=" .. tostring(result.Passed)
                .. " checks=" .. tostring(result.ChecksRun)
                .. " message=" .. tostring(result.Message))
        end
    end
end

function BeginPlay()
    seq_comp = find_sequence_component()
    if seq_comp == nil then
        log("no ActorSequenceComponent on " .. tostring(obj and obj.UUID))
    else
        log("ready on " .. obj.UUID)
    end
    run_round_trip_self_test()
end

function Tick(dt)
    if seq_comp == nil then
        seq_comp = find_sequence_component()
        return
    end

    if Input.GetKeyDown("F2") then
        seq_comp:Play()
        log("Play")
    elseif Input.GetKeyDown("F3") then
        seq_comp:Pause()
        log("Pause")
    elseif Input.GetKeyDown("F4") then
        seq_comp:Stop()
        log("Stop")
    end
end
