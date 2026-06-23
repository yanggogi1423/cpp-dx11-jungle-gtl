local EventBus = {}
EventBus.__index = EventBus

function EventBus.new()
    return setmetatable({
        listeners = {},
        next_token = 0
    }, EventBus)
end

function EventBus:Subscribe(event_name, owner, callback)
    if type(owner) == "function" and callback == nil then
        callback = owner
        owner = nil
    end

    if type(event_name) ~= "string" or type(callback) ~= "function" then
        return nil
    end

    self.next_token = self.next_token + 1
    local token = {
        id = self.next_token,
        event = event_name,
        owner = owner,
        callback = callback
    }

    local list = self.listeners[event_name]
    if list == nil then
        list = {}
        self.listeners[event_name] = list
    end

    table.insert(list, token)
    return token
end

function EventBus:Unsubscribe(token)
    if token == nil or token.event == nil then
        return false
    end

    local list = self.listeners[token.event]
    if list == nil then
        return false
    end

    for i = #list, 1, -1 do
        if list[i] == token or list[i].id == token.id then
            table.remove(list, i)
            return true
        end
    end

    return false
end

function EventBus:ClearOwner(owner)
    if owner == nil then
        return 0
    end

    local removed = 0
    for _, list in pairs(self.listeners) do
        for i = #list, 1, -1 do
            if list[i].owner == owner then
                table.remove(list, i)
                removed = removed + 1
            end
        end
    end
    return removed
end

function EventBus:Publish(event_name, payload)
    local list = self.listeners[event_name]
    if list == nil or #list == 0 then
        return 0
    end

    local snapshot = {}
    for i = 1, #list do
        snapshot[i] = list[i]
    end

    local delivered = 0
    for _, listener in ipairs(snapshot) do
        local ok, err = pcall(listener.callback, payload, event_name)
        if not ok then
            print("[EventBus] listener failed for " .. tostring(event_name) .. ": " .. tostring(err))
        else
            delivered = delivered + 1
        end
    end

    return delivered
end

function EventBus:Clear()
    self.listeners = {}
end

return EventBus
