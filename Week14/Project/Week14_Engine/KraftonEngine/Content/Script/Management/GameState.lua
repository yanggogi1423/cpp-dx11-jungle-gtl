local GameState = {
    Intro = "Intro",
    Main = "Main",
    Loading = "Loading",
    PreInGame = "Pre-InGame",
    InGame = "InGame",
    Defeat1 = "Defeat1",
    Defeat2 = "Defeat2",
    Victory = "Victory"
}

GameState.Values = {
    GameState.Intro,
    GameState.Main,
    GameState.Loading,
    GameState.PreInGame,
    GameState.InGame,
    GameState.Defeat1,
    GameState.Defeat2,
    GameState.Victory
}

local valid_states = {}
for _, state in ipairs(GameState.Values) do
    valid_states[state] = true
end

function GameState.IsValid(state)
    return valid_states[state] == true
end

return GameState
