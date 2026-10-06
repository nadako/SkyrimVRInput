#pragma once

// copied from SKSEVR as CommonLibNG doesn't seem to have the full list...
enum GameInputContext : uint32_t {
	kContext_Gameplay = 0,
	kContext_MenuMode,
	kContext_Console,
	kContext_ItemMenu,
	kContext_Inventory,
	kContext_DebugText,
	kContext_Favorites,
	kContext_Map,
	kContext_Stats,
	kContext_Cursor,
	kContext_Book,
	kContext_DebugOverlay,
	kContext_Journal,
	kContext_TFCMode,
	kContext_MapDebug,
	kContext_Lockpicking,
	kContext_Favor,
	kContext_CraftingMenu,
	kContext_BarterMenu,
	kContext_RaceSexMenu,
	kContext_DialogueMenu,

	kContext_ImGuiHelper, // fake context for imgui
	kContextCount
};

constexpr auto InvalidInputContext = kContextCount;

enum Hand {
	HandLeft,
	HandRight,
	HandCount,
	HandNone = 0xFF
};

enum ActionType {
	ActionTypeButton, // boolean, a bit in ulButtonPressed
	ActionTypeTouch, // boolean, a bit in ulButtonTouched
	ActionTypeAxis1, // vector1, a value in rAxis
	ActionTypeAxis2, // vector2, a value in rAxis
};

enum AxisIndex : uint8_t {
	AxisJoystick,
	AxisTrigger,
	AxisGrip, // TODO: VRIK uses that as a "grip proximity" value, apparently Index reports that, not sure about Frame
	Axis3,
	Axis4,
	AxisCount
};

static_assert(AxisCount == vrnew::k_unControllerStateAxisCount);

inline vrnew::EVRControllerAxisType GetControllerAxisType([[maybe_unused]] int axisIndex) {
	// the actual joystick axis must be reported as joystick type, otherwise the game won't read its values, and we use it for move/look
	// not sure what to do with the others, because we don't use them, i don't think the game uses them and I'm yet to find a mod that uses them
	// for now, let's report all of them as joysticks (so having 2d input)
	return vrnew::k_eControllerAxis_Joystick;
}

// it doesn't matter which hand the action is assigned to by SteamVR input system, but for the legacy API used by Skyrim
// we need to transform that into a per-hand controller state value, which is then mapped by the game to an actual action
// through controlmapvr.txt
struct ActionLegacyControl {
	Hand hand;

	union {
		uint8_t code;
		AxisIndex axis;
	};
};

// in the Skyrim control system, there is a context stack, and bindings assigned from an upper context override
// ones from the context below. this way, for example, joystick or trigger actions get different meanings, while
// we still can re-use common bindings like menu navigation in all kinds of menus that have their own buttons
// with SteamVR actions, we want to keep specific action overriding others based on game input context,
// but since we have our own independent "action set" grouping and allow for arbitrary button bindings,
// we need some mechanism to preserve the action priorities, here it is: every action has a context id that it's bound to
// and "stacked id" value - when non-zero, the actions with the same id are prioritized based on the current context stack
// Example:
//  - "gameplay/RightTrigger" has (Gameplay, 1) and maps to "Main Gameplay / Right Attack/Block" binding
//  - "menu/Accept" has (MenuMode, 1) and maps to "Menu Mode / Accept" binding
//  - "items/EquipRight" has (ItemMenu, 1) and maps to "Item Menus / RightEquip" binding
// all of them might be bound to right trigger, but since we process actions per input context, if we are in an item menu and
// we registered the items/EquipRight action, we store that "action 1" was handled, so when processing further contexts, all the actions
// with the same "stacked id"s are ignored.
// similarly, if the user has pressed "menu/Accept" bound to some other button (i.e. A) and we're in the item menu, we don't handle "action 1"
// within the ItemMenu context, but react to one in the MenuMode and ignore one in Gameplay
// "react" means state changes for buttons or any value reads for axes (so axes always take precedence)
struct ActionContext {
	GameInputContext contextId;
	int32_t stackedId; // -1 means this action is only handled when its context is at the top
};

struct ActionDef {
	const char *name;
	ActionContext context;
	ActionType type;

	ActionLegacyControl control;
};

// known legacy button codes for sensible re-use
namespace KnownButtons {
	constexpr auto AX = 0x07; // EVRButtonId::k_EButton_A
	constexpr auto BY = 0x01; // EVRButtonId::k_EButton_ApplicationMenu

	// NOTE: in gameplay, these should keep their trigger/grip meanings, because other mods (e.g. HIGGS) assume that
	constexpr auto Trigger = 0x21; // 33 EVRButtonId::k_EButton_Axis1
	constexpr auto Grip = 0x02; // EVRButtonId::k_EButton_Grip

	constexpr auto Joystick = 0x20; // 32 EVRButtonId::k_EButton_Axis0
	constexpr auto Axis2 = 0x22; // 34 EVRButtonId::k_EButton_Axis2 (also known as "GripAlt", don't use because IsGripButton)
	constexpr auto Axis3 = 0x23; // 35 EVRButtonId::k_EButton_Axis3 (also known as "TouchpadClick")
	constexpr auto Axis4 = 0x24; // 36 EVRButtonId::k_EButton_Axis4 (also known as "TouchpadAlt")
}

namespace StackedActions {
	constexpr auto MenuSelect = 1; // item menu equip left/right and general menu accept conflict, so if they are bound to the same button, equip should win
	constexpr auto MenuNav = 2; // skills navigation stick should win over menu nav buttons in stats (which don't do anything anyway, so let's not even produce events)
}

namespace actionData {
	inline constexpr const char *sets[] = {
		"/actions/gameplay",
		"/actions/menu",
		"/actions/map",
		"/actions/lockpicking",
		"/actions/skills",
		"/actions/imgui",
	};

	// TODO: check codes against RE::BSOpenVRControllerDevice::Keys so we don't break things like RE::BSOpenVRControllerDevice::IsGripButton and similar
	// TODO: what is Favor context, it has the cancel button, check and bind. is it the follower "i want you to do something one?"
	inline constexpr ActionDef defs[] = {
		// @formatter:off preserve table layout
		// NOTE: PLANCK (activeragdoll) relies on the first joystick input to determine movement for power attacks!
		// TODO: it also checks for g_leftHandedMode, so we should switch them ideally, but handedness is a bigger topic...
		{"/actions/gameplay/in/Move",          {kContext_Gameplay, -1}, ActionTypeAxis2, {HandLeft,  AxisJoystick}},
		{"/actions/gameplay/in/TurnJumpSneak", {kContext_Gameplay, -1}, ActionTypeAxis2, {HandRight, AxisJoystick}},
		{"/actions/gameplay/in/Turn",          {kContext_Gameplay, -1}, ActionTypeAxis1, {HandRight, AxisJoystick}}, // allow binding turning-only, this is intentionally Axis1 so we zero out the y value, so the game doesn't do jump/sneak

		{"/actions/gameplay/in/LeftTrigger",   {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  KnownButtons::Trigger}},
		{"/actions/gameplay/in/RightTrigger",  {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, KnownButtons::Trigger}},
		{"/actions/gameplay/in/LeftGrip",      {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  KnownButtons::Grip}},
		{"/actions/gameplay/in/RightGrip",     {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, KnownButtons::Grip}},

		{"/actions/gameplay/in/Jump",          {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x03}},
		{"/actions/gameplay/in/Sneak",         {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, KnownButtons::Joystick}},
		{"/actions/gameplay/in/Sprint",        {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  KnownButtons::Joystick}},
		{"/actions/gameplay/in/Activate",      {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, KnownButtons::AX}},
		{"/actions/gameplay/in/TweenMenu",     {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  KnownButtons::AX}},

		{"/actions/gameplay/in/OpenMagic",     {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x04}},
		{"/actions/gameplay/in/OpenItems",     {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x06}},
		{"/actions/gameplay/in/OpenSkills",    {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x08}},
		{"/actions/gameplay/in/OpenMap",       {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x09}},

		{"/actions/gameplay/in/ReadyWeapon",   {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  0x04}},
		{"/actions/gameplay/in/ShoutPower",    {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, 0x05}},
		{"/actions/gameplay/in/Favorites",     {kContext_Gameplay, -1}, ActionTypeButton, {HandRight, KnownButtons::BY}},
		{"/actions/gameplay/in/Wait",          {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  0x08}},
		{"/actions/gameplay/in/Journal",       {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  KnownButtons::BY}},
		{"/actions/gameplay/in/QuickSave",     {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  0x05}},
		{"/actions/gameplay/in/QuickLoad",     {kContext_Gameplay, -1}, ActionTypeButton, {HandLeft,  0x06}},

		// buttons for mods, let the mods decide in which contexts they work, i guess
		{"/actions/gameplay/in/LeftAxis3Button",  {kContext_Gameplay, 0}, ActionTypeButton, {HandLeft,  KnownButtons::Axis3}},
		{"/actions/gameplay/in/RightAxis3Button", {kContext_Gameplay, 0}, ActionTypeButton, {HandRight, KnownButtons::Axis3}},
		{"/actions/gameplay/in/LeftAxis4Button",  {kContext_Gameplay, 0}, ActionTypeButton, {HandLeft,  KnownButtons::Axis4}},
		{"/actions/gameplay/in/RightAxis4Button", {kContext_Gameplay, 0}, ActionTypeButton, {HandRight, KnownButtons::Axis4}},

		{"/actions/gameplay/in/LeftTriggerTouch",  {kContext_Gameplay, 0}, ActionTypeTouch, {HandLeft,  KnownButtons::Trigger}},
		{"/actions/gameplay/in/RightTriggerTouch", {kContext_Gameplay, 0}, ActionTypeTouch, {HandRight, KnownButtons::Trigger}},
		{"/actions/gameplay/in/LeftGripTouch",     {kContext_Gameplay, 0}, ActionTypeTouch, {HandLeft,  KnownButtons::Grip}},
		{"/actions/gameplay/in/RightGripTouch",    {kContext_Gameplay, 0}, ActionTypeTouch, {HandRight, KnownButtons::Grip}},
		{"/actions/gameplay/in/LeftThumbTouch",    {kContext_Gameplay, 0}, ActionTypeTouch, {HandLeft,  KnownButtons::Joystick}},
		{"/actions/gameplay/in/RightThumbTouch",   {kContext_Gameplay, 0}, ActionTypeTouch, {HandRight, KnownButtons::Joystick}},

		// TODO: Hotkey(1-8) buttons - do they work in VR? if so they could be used for mods in addition to Axis3/Axis4
		// TODO: we should probably have a separate set for item menus, in which one could e.g. bind Left/Right to the same buttons as journal TabLeft/TabRight for consistency
		// NOTE: this is placed above Accept, so it can take priority if used within the same context
		{"/actions/menu/in/EquipLeft",         {kContext_ItemMenu, StackedActions::MenuSelect}, ActionTypeButton, {HandLeft,  KnownButtons::Trigger}},
		{"/actions/menu/in/EquipRight",        {kContext_ItemMenu, StackedActions::MenuSelect}, ActionTypeButton, {HandRight, KnownButtons::Trigger}},

		{"/actions/menu/in/Accept",            {kContext_MenuMode, StackedActions::MenuSelect}, ActionTypeButton, {HandRight, KnownButtons::AX}},
		{"/actions/menu/in/Cancel",            {kContext_MenuMode, 0}, ActionTypeButton, {HandRight, KnownButtons::Grip}},
		{"/actions/menu/in/ActionA",           {kContext_MenuMode, 0}, ActionTypeButton, {HandLeft, KnownButtons::AX}}, // drop items / take all / craft
		{"/actions/menu/in/ActionB",           {kContext_MenuMode, 0}, ActionTypeButton, {HandLeft, KnownButtons::BY}}, // favorite / search (in crafting menus)
		{"/actions/menu/in/Up",                {kContext_MenuMode, StackedActions::MenuNav}, ActionTypeButton, {HandRight, 0x03}},
		{"/actions/menu/in/Down",              {kContext_MenuMode, StackedActions::MenuNav}, ActionTypeButton, {HandRight, 0x04}},
		{"/actions/menu/in/Left",              {kContext_MenuMode, StackedActions::MenuNav}, ActionTypeButton, {HandRight, 0x05}},
		{"/actions/menu/in/Right",             {kContext_MenuMode, StackedActions::MenuNav}, ActionTypeButton, {HandRight, 0x06}},

		{"/actions/menu/in/TabLeft",           {kContext_Journal, 0}, ActionTypeButton, {HandLeft,  0x08}},
		{"/actions/menu/in/TabRight",          {kContext_Journal, 0}, ActionTypeButton, {HandRight, 0x08}},

		{"/actions/menu/in/ZoomItem",          {kContext_ItemMenu, 0},                          ActionTypeButton, {HandRight, KnownButtons::Joystick}},
		{"/actions/menu/in/RotateItem",        {kContext_ItemMenu, 0},                          ActionTypeAxis2,  {HandLeft,  AxisJoystick}},

		{"/actions/menu/in/ChargeItem",        {kContext_Inventory, 0}, ActionTypeButton, {HandLeft, 0x03}},

		// TODO: check how this works with left-handedness (and how to deal with it within the context of skyrim in general)
		{"/actions/map/in/Click",              {kContext_Map, 0}, ActionTypeButton, {HandRight, KnownButtons::Trigger}},
		{"/actions/map/in/ExitMap",            {kContext_Map, 0}, ActionTypeButton, {HandRight, KnownButtons::Grip}},
		{"/actions/map/in/LocalMap",           {kContext_Map, 0}, ActionTypeButton, {HandRight, KnownButtons::AX}},
		{"/actions/map/in/PlayerPosition",     {kContext_Map, 0}, ActionTypeButton, {HandRight, KnownButtons::BY}},
		{"/actions/map/in/Journal",            {kContext_Map, 0}, ActionTypeButton, {HandLeft,  KnownButtons::BY}},
		{"/actions/map/in/Move",               {kContext_Map, 0}, ActionTypeAxis2,  {HandLeft,  AxisJoystick}}, // NOTE: there's no "Move" binding for Map Menu context, but emitting the gameplay move axis does the trick
		{"/actions/map/in/Turn",               {kContext_Map, 0}, ActionTypeAxis2,  {HandRight, AxisJoystick}}, // NOTE: the "Look" can be left unbound too, emitting the gameplay look axis does the trick

		{"/actions/lockpicking/in/RotatePick", {kContext_Lockpicking, 0}, ActionTypeAxis2,  {HandLeft,  AxisJoystick}},
		{"/actions/lockpicking/in/RotateLock", {kContext_Lockpicking, 0}, ActionTypeAxis2,  {HandRight, AxisJoystick}},
		{"/actions/lockpicking/in/Cancel",     {kContext_Lockpicking, 0}, ActionTypeButton, {HandRight, KnownButtons::Grip}},

		{"/actions/skills/in/Navigate",        {kContext_Stats, StackedActions::MenuNav},   ActionTypeAxis2,  {HandLeft, AxisJoystick}},
		{"/actions/skills/in/Select",          {kContext_Stats, StackedActions::MenuSelect}, ActionTypeButton, {HandRight, KnownButtons::Trigger}},

		// to support ImGui VR Helper we emulate the legacy buttons it expects (with its default open/close combos) in a special fake context
		// NOTE: some actions are listed twice to emit two buttons at once, this is kind of a hack, but it's fine for now
		{"/actions/menu/in/OpenImGui",         {kContext_MenuMode, 0},     ActionTypeButton, {HandLeft,  KnownButtons::AX}},
		{"/actions/menu/in/OpenImGui",         {kContext_MenuMode, 0},     ActionTypeButton, {HandLeft,  KnownButtons::BY}},
		{"/actions/imgui/in/Close",            {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandLeft,  KnownButtons::Grip}},
		{"/actions/imgui/in/Close",            {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandRight, KnownButtons::Grip}},
		{"/actions/imgui/in/LeftTrigger",      {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandLeft,  KnownButtons::Trigger}},
		{"/actions/imgui/in/RightTrigger",     {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandRight, KnownButtons::Trigger}},
		{"/actions/imgui/in/LeftGrip",         {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandLeft,  KnownButtons::Grip}},
		{"/actions/imgui/in/RightGrip",        {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandRight, KnownButtons::Grip}},
		{"/actions/imgui/in/LeftStick",        {kContext_ImGuiHelper, 0},  ActionTypeAxis2,  {HandLeft,  AxisJoystick}},
		{"/actions/imgui/in/RightStick",       {kContext_ImGuiHelper, 0},  ActionTypeAxis2,  {HandRight, AxisJoystick}},
		{"/actions/imgui/in/LeftStickButton",  {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandLeft,  KnownButtons::Joystick}},
		{"/actions/imgui/in/RightStickButton", {kContext_ImGuiHelper, 0},  ActionTypeButton, {HandRight, KnownButtons::Joystick}},
		// @formatter:on
	};

	struct AdditionalActionContext {
		const char *name;
		GameInputContext context;
	};

	inline constexpr AdditionalActionContext additionalActionContexts[] = {
		// @formatter:off
		// crafting menu is not an item menu, but it does support item rotation (zoom didn't work for me but let's forward it in case it gets fixed)
		{"/actions/menu/in/ZoomItem",   kContext_CraftingMenu},
		{"/actions/menu/in/RotateItem", kContext_CraftingMenu},

		{"/actions/menu/in/ZoomItem",   kContext_BarterMenu},
		{"/actions/menu/in/RotateItem", kContext_BarterMenu},

		// dialogue is not a "menu mode" context at all, but it has exactly the same basic actions, let's reuse them
		{"/actions/menu/in/Accept", kContext_DialogueMenu},
		{"/actions/menu/in/Cancel", kContext_DialogueMenu},
		{"/actions/menu/in/Up",     kContext_DialogueMenu},
		{"/actions/menu/in/Down",   kContext_DialogueMenu},
		{"/actions/menu/in/Left",   kContext_DialogueMenu},
		{"/actions/menu/in/Right",  kContext_DialogueMenu},

		// favorites is similarly not a "menu mode", but it has exactly the same basic actions (no left/right too, but let's have it, in case of mods maybe)
		{"/actions/menu/in/Accept", kContext_Favorites},
		{"/actions/menu/in/Cancel", kContext_Favorites},
		{"/actions/menu/in/Up",     kContext_Favorites},
		{"/actions/menu/in/Down",   kContext_Favorites},
		{"/actions/menu/in/Left",   kContext_Favorites},
		{"/actions/menu/in/Right",  kContext_Favorites},

		// also equipment buttons work the same way as in the items menu
		{"/actions/menu/in/EquipLeft",  kContext_Favorites},
		{"/actions/menu/in/EquipRight", kContext_Favorites},

		// race/sex menu has the same tab buttons as journal
		{"/actions/menu/in/TabLeft",  kContext_RaceSexMenu},
		{"/actions/menu/in/TabRight", kContext_RaceSexMenu},
		// @formatter:on
	};
}

constexpr uint32_t actionSetCount = std::size(actionData::sets);
constexpr uint32_t actionCount = std::size(actionData::defs);

// ulButtonPressed / ulButtonTouched bit
inline uint64_t GetButtonCodeMask(uint64_t code) {
	return vrnew::ButtonMaskFromId((vrnew::EVRButtonId) code);
}

inline RE::BSTArray<GameInputContext> &GetInputContextPriorityStack() {
	RE::BSTArray<RE::UserEvents::INPUT_CONTEXT_ID> &stack = RE::ControlMap::GetSingleton()->GetRuntimeData().contextPriorityStack;
	return reinterpret_cast<RE::BSTArray<GameInputContext> &>(stack);
}
