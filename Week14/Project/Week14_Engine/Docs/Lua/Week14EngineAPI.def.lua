---@meta

-- Week14 Engine Lua API quick definitions.
-- This file is for editor autocomplete/documentation only. Do not require/dofile it.

---@alias KeyName string|integer

---@class Vector
---@field X number
---@field Y number
---@field Z number

---@class Rotator
---@field Pitch number
---@field Yaw number
---@field Roll number

---@class Actor
---@field UUID string
local Actor = {}

---@return ActorSequenceComponent|nil
function Actor:GetActorSequenceComponent() end

---@param tag string
---@return boolean
function Actor:ActorHasTag(tag) end

---@param tag string
---@return Component|nil
function Actor:FindComponentByTag(tag) end

---@vararg string
---@return Component|nil
function Actor:FindComponentByTags(...) end

---@class Component
local Component = {}

---@return ActorSequenceComponent|nil
function Component:AsActorSequenceComponent() end

---@class UserWidget
local UserWidget = {}

---@param zOrder? integer
function UserWidget:AddToViewport(zOrder) end

function UserWidget:RemoveFromParent() end

---@param elementId string
---@param text string
function UserWidget:SetText(elementId, text) end

---@param elementId string
---@return string
function UserWidget:GetText(elementId) end

---@param elementId string
---@param value string|number
---@return boolean
function UserWidget:SetValue(elementId, value) end

---@param elementId string
---@return string
function UserWidget:GetValue(elementId) end

---@param elementId string
---@param className string
---@param enabled boolean
---@return boolean
function UserWidget:SetClass(elementId, className, enabled) end

---@param elementId string
---@param styleName string
---@param value string
---@return boolean
function UserWidget:SetStyle(elementId, styleName, value) end

---@param elementId string
---@param imagePath string
---@return boolean
function UserWidget:SetImage(elementId, imagePath) end

---@param elementId string
---@param value number
---@return boolean
function UserWidget:SetProgress(elementId, value) end

---@param elementId string
---@param zOrder integer
---@return boolean
function UserWidget:SetZOrder(elementId, zOrder) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UserWidget:SetTint(elementId, r, g, b, a) end

---@param elementId string
---@param alpha number
---@return boolean
function UserWidget:SetAlpha(elementId, alpha) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UserWidget:SetTextColor(elementId, r, g, b, a) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UserWidget:SetBackgroundColor(elementId, r, g, b, a) end

---@param elementId string
---@param pixels number
---@return boolean
function UserWidget:SetRounding(elementId, pixels) end

---@param elementId string
---@param emScale number
---@return boolean
function UserWidget:SetFontScale(elementId, emScale) end

---@param elementId string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function UserWidget:SetElementTransform(elementId, x, y, width, height) end

---@param elementId string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function UserWidget:SetTransform(elementId, x, y, width, height) end

---@param elementId string
---@param propertyName string
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:SetTransition(elementId, propertyName, duration, timing, delay) end

---@param elementId string
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:SetTransitionAll(elementId, duration, timing, delay) end

---@param elementId string
---@return boolean
function UserWidget:ClearTransition(elementId) end

---@param elementId string
---@param alpha number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:AnimateAlpha(elementId, alpha, duration, timing, delay) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:AnimateTextColor(elementId, r, g, b, a, duration, timing, delay) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:AnimateBackgroundColor(elementId, r, g, b, a, duration, timing, delay) end

---@param elementId string
---@param x number
---@param y number
---@param width number
---@param height number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:AnimateTransform(elementId, x, y, width, height, duration, timing, delay) end

---@param elementId string
---@param className string
---@param enabled boolean
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UserWidget:AnimateClass(elementId, className, enabled, duration, timing, delay) end

---@param elementId string
---@return boolean
function UserWidget:RemoveElement(elementId) end

---@param elementId string
---@param visible boolean
---@return boolean
function UserWidget:SetVisible(elementId, visible) end

---@param elementId string
---@param enabled boolean
---@return boolean
function UserWidget:SetEnabled(elementId, enabled) end

---@param elementId string
---@param eventName string
---@return boolean
function UserWidget:SetActionEvent(elementId, eventName) end

---@return string[]
function UserWidget:PollActionEvents() end

---@param wantsMouse boolean
function UserWidget:SetWantsMouse(wantsMouse) end

---@param wantsKeyboard boolean
function UserWidget:SetWantsKeyboard(wantsKeyboard) end

---@param wantsTextInput boolean
function UserWidget:SetWantsTextInput(wantsTextInput) end

---@param blocksGameInput boolean
function UserWidget:SetBlocksGameInput(blocksGameInput) end

---@param blocksGameKeyboard boolean
function UserWidget:SetBlocksGameKeyboard(blocksGameKeyboard) end

---@param blocksGameMouseLook boolean
function UserWidget:SetBlocksGameMouseLook(blocksGameMouseLook) end

---@class ActorSequence
local ActorSequence = {}

---@return number
function ActorSequence:GetStartTime() end

---@return number
function ActorSequence:GetDuration() end

---@return number
function ActorSequence:GetEndTime() end

---@param startTime number
function ActorSequence:SetStartTime(startTime) end

---@param duration number
function ActorSequence:SetDuration(duration) end

---@param startTime number
---@param duration number
function ActorSequence:SetPlaybackRange(startTime, duration) end

function ActorSequence:Clear() end

---@return string
function ActorSequence:ExportToJsonString() end

---@param json string
---@return boolean
function ActorSequence:ImportFromJsonString(json) end

---@class ActorSequencePlayer
local ActorSequencePlayer = {}

---@param resetTime? boolean
function ActorSequencePlayer:Play(resetTime) end

function ActorSequencePlayer:Pause() end

---@param restoreBaseValues? boolean
function ActorSequencePlayer:Stop(restoreBaseValues) end

---@param time number
function ActorSequencePlayer:SetCurrentTime(time) end

---@return number
function ActorSequencePlayer:GetCurrentTime() end

---@return boolean
function ActorSequencePlayer:IsPlaying() end

---@return boolean
function ActorSequencePlayer:IsPaused() end

---@class ActorSequenceTrackDesc
---@field target? string|Actor|Component
---@field targetName? string
---@field componentName? string
---@field componentTag? string
---@field property string
---@field channel? string
---@field startTime? number
---@field duration? number
---@field value? number

---@class ActorSequenceComponent: Component
local ActorSequenceComponent = {}

function ActorSequenceComponent:Play() end
function ActorSequenceComponent:Pause() end
function ActorSequenceComponent:Stop() end

---@return ActorSequence
function ActorSequenceComponent:GetSequence() end

---@return ActorSequencePlayer
function ActorSequenceComponent:GetSequencePlayer() end

---@param desc ActorSequenceTrackDesc
---@return boolean
function ActorSequenceComponent:AddFloatTrack(desc) end

---@class InputAPI
Input = {}

---@param key KeyName
---@return boolean
function Input.GetKeyDown(key) end

---@param key KeyName
---@return boolean
function Input.GetKey(key) end

---@param key KeyName
---@return boolean
function Input.GetKeyUp(key) end

---@return string
function Input.ConsumeTextInput() end

---@param mode "GameOnly"|"GameAndUI"|"UIOnly"
function Input.SetInputMode(mode) end

function Input.SetInputModeGameOnly() end
function Input.SetInputModeGameAndUI() end
function Input.SetInputModeUIOnly() end

---@param visible boolean
function Input.SetCursorVisible(visible) end

---@param captured boolean
function Input.SetMouseCaptured(captured) end

function Input.ReleaseMouseCapture() end

---@class UIAPI
UI = {}

---@param documentPath string
---@return UserWidget|nil
function UI.CreateWidget(documentPath) end

---@param elementId string
---@param text string
---@return boolean
function UI.SetText(elementId, text) end

---@param elementId string
---@param value string|number
---@return boolean
function UI.SetValue(elementId, value) end

---@param elementId string
---@param className string
---@param enabled boolean
---@return boolean
function UI.SetClass(elementId, className, enabled) end

---@param elementId string
---@param styleName string
---@param value string
---@return boolean
function UI.SetStyle(elementId, styleName, value) end

---@param elementId string
---@param imagePath string
---@return boolean
function UI.SetImage(elementId, imagePath) end

---@param elementId string
---@param value number
---@return boolean
function UI.SetProgress(elementId, value) end

---@param elementId string
---@param zOrder integer
---@return boolean
function UI.SetZOrder(elementId, zOrder) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UI.SetTint(elementId, r, g, b, a) end

---@param elementId string
---@param alpha number
---@return boolean
function UI.SetAlpha(elementId, alpha) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UI.SetTextColor(elementId, r, g, b, a) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@return boolean
function UI.SetBackgroundColor(elementId, r, g, b, a) end

---@param elementId string
---@param pixels number
---@return boolean
function UI.SetRounding(elementId, pixels) end

---@param elementId string
---@param emScale number
---@return boolean
function UI.SetFontScale(elementId, emScale) end

---@param elementId string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function UI.SetElementTransform(elementId, x, y, width, height) end

---@param elementId string
---@param x number
---@param y number
---@param width number
---@param height number
---@return boolean
function UI.SetTransform(elementId, x, y, width, height) end

---@param elementId string
---@param alpha number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.AnimateAlpha(elementId, alpha, duration, timing, delay) end

---@param elementId string
---@param propertyName string
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.SetTransition(elementId, propertyName, duration, timing, delay) end

---@param elementId string
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.SetTransitionAll(elementId, duration, timing, delay) end

---@param elementId string
---@return boolean
function UI.ClearTransition(elementId) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.AnimateTextColor(elementId, r, g, b, a, duration, timing, delay) end

---@param elementId string
---@param r number
---@param g number
---@param b number
---@param a? number
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.AnimateBackgroundColor(elementId, r, g, b, a, duration, timing, delay) end

---@param elementId string
---@param className string
---@param enabled boolean
---@param duration number
---@param timing? string
---@param delay? number
---@return boolean
function UI.AnimateClass(elementId, className, enabled, duration, timing, delay) end

---@param elementId string
---@return boolean
function UI.RemoveElement(elementId) end

---@param elementId string
---@param visible boolean
---@return boolean
function UI.SetVisible(elementId, visible) end

---@param elementId string
---@param enabled boolean
---@return boolean
function UI.SetEnabled(elementId, enabled) end

---@param elementId string
---@param eventName string
---@return boolean
function UI.SetActionEvent(elementId, eventName) end

---@return string[]
function UI.PollActionEvents() end

---@class SceneAPI
Scene = {}

---@param pathOrName string
---@return boolean
function Scene.Open(pathOrName) end

---@param pathOrName string
---@return boolean
function Scene.Load(pathOrName) end

---@param pathOrName string
---@return boolean
function Scene.TransitionTo(pathOrName) end

---@return boolean
function Scene.Reload() end

---@return boolean
function Scene.IsOpenPending() end

---@return string
function Scene.GetCurrentPath() end

---@class AudioAPI
Audio = {}

---@param pathOrKey string
---@param volumeScale? number
---@return integer
function Audio.PlaySFXHandle(pathOrKey, volumeScale) end

---@param pathOrKey string
---@param position Vector
---@param volumeScale? number
---@return integer
function Audio.PlaySFX3D(pathOrKey, position, volumeScale) end

---@param handle integer
function Audio.StopSound(handle) end

---@param handle integer
---@return boolean
function Audio.IsSoundPlaying(handle) end

---@param handle integer
---@param position Vector
function Audio.SetSoundPosition(handle, position) end

---@param pathOrKey string
---@param maxConcurrent integer
---@param cooldownSeconds number
---@param priority integer
---@param stopOldest boolean
function Audio.SetSFXPolicy(pathOrKey, maxConcurrent, cooldownSeconds, priority, stopOldest) end

---@class CameraManagerAPI
CameraManager = {}

---@param actorName string
---@param blendTime? number
---@return boolean
function CameraManager.ToggleActorCamera(actorName, blendTime) end

---@param blendTime? number
---@return boolean
function CameraManager.ToggleOwnerCamera(blendTime) end

---@param actor Actor
---@param blendTime? number
---@return boolean
function CameraManager.PossessCamera(actor, blendTime) end

---@param target Actor
---@param blendTime? number
---@return boolean
function CameraManager.SetViewTargetWithBlend(target, blendTime) end

---@param scale? number
function CameraManager.StartWaveShake(scale) end

---@param scale? number
function CameraManager.StartSequenceShake(scale) end

---@param assetPath string
---@param scale? number
function CameraManager.StartCameraShakeAsset(assetPath, scale) end

---@param radius number
---@param outerBlurRadius number
---@param zoomFov number
---@param feather? number
---@param edgeBlurRadius? number
---@param intensity? number
---@param lookSensitivityScale? number
---@param blendTime? number
function CameraManager.SetScopeLensProfile(radius, outerBlurRadius, zoomFov, feather, edgeBlurRadius, intensity, lookSensitivityScale, blendTime) end

---@param enabled boolean
function CameraManager.SetScopeZoomEnabled(enabled) end

---@return boolean
function CameraManager.ToggleScopeZoom() end

---@return boolean
function CameraManager.IsScopeZoomEnabled() end

function CameraManager.ClearScopeLens() end

---@class DebugAPI
Debug = {}

---@class ActorSequenceDiagnosticsResult
---@field Passed boolean
---@field ChecksRun integer
---@field Message string

---@return ActorSequenceDiagnosticsResult
function Debug.RunActorSequenceRoundTripSelfTest() end

---@type Actor
obj = obj
