import re
from pathlib import Path

file_dir = Path(__file__).parent
root_dir = file_dir.parent
source_file = file_dir / "controlmapvr.orig.txt"
target_file = root_dir / "data/interface/controls/pc/controlmapvr.txt"

# input context -> event -> (right, left)
# our steamvr actions map into the hand+code pair and this maps those into skyrim input events

menu_mode_basic_button_codes = {
    "Accept": (0x07, None),
    "Cancel": (0x02, None),
    "Up": (0x03, None),
    "Down": (0x04, None),
    "Left": (0x05, None),
    "Right": (0x06, None),
}
item_zoom_rotate_button_codes = {
    "Item Zoom": (0x20, None),
    "Rotate": (None, 0x0b),
}
tab_left_right_button_codes = {
    # this is a single binding that is supported in Journal and RaceSexMenu
    # our Tab Left/Right actions will invoke either left or right button, so this just works
    "Tab By Handedness": (0x08, 0x08),
}
button_codes = {
    "Main Gameplay": {
        # as far as I understand, the engine would emit events with these codes for axis0 (joystick) of respective hands and it's hard-coded,
        # but we still need to map it to bind the action to the axis
        "Move": (None, 0x0b),
        "Look": (0x0c, None),

        "Jump": (0x03, None),
        "Sneak": (0x20, None),

        "Left Attack/Block": (None, 0x21),
        "Right Attack/Block": (0x21, None),

        "Activate": (0x07, None),
        "Tween Menu": (None, 0x07),
        "Quick Inventory": (0x06, None),
        "Quick Magic": (0x04, None),
        "Quick Stats": (0x08, None),
        "Quick Map": (0x09, None),
        "Ready Weapon": (None, 0x04),
        "Shout": (0x05, None),
        "Sprint": (None, 0x20),
        "Favorites": (0x01, None),
        "Wait": (None, 0x08),
        "Journal": (None, 0x01), # Journal seem to behave like Pause, opening the same exact tab...
        "Quicksave": (None, 0x05),
        "Quickload": (None, 0x06),
    },
    "Menu Mode": menu_mode_basic_button_codes | {
        "XButton": (None, 0x07),
        "YButton": (None, 0x01),
    },
    "Console": {},
    "Item Menus": item_zoom_rotate_button_codes | {
        "LeftEquip": (None, 0x21),
        "RightEquip": (0x21, None),
    },
    "Inventory": {
        "ChargeItem": (None, 0x03),
    },
    "Debug Text": {},
    "Favorites menu": menu_mode_basic_button_codes  # original controlmapvr didn't have Left/Right for favorites, this adds them, but it's a vertical menu, so... maybe for mods?
        | {
          # these were not present in controlmapvr either, but they work as expected, so let's use them
        "LeftEquip": (None, 0x21),
        "RightEquip": (0x21, None),
    },
    "Map Menu": {
        "Click": (0x21, None),
        "Cancel": (0x02, None),
        "LocalMap": (0x07, None),
        "PlayerPosition": (0x01, None),
        "Journal": (None, 0x01),
        # "Look": (0x0c, None), # not needed as the game will detect Gameplay look input
    },
    "Stats": {
        "Rotate": (None, 0x0b),
        "Click": (0x21, None),
        # others don't have any effect (well look does rotate in initial selection, but Rotate does the same job, so no point)
    },
    "Cursor": {},
    "Book": {},
    "Debug overlay": {},
    "Journal": tab_left_right_button_codes,
    "TFC mode": {},
    "Debug Map Menu-like mode (but not the actual map menu)": {},
    "Lockpicking": {
        "RotatePick": (None, 0x0b),
        "RotateLock": (0x0c, None),
        "Cancel": (0x02, None),
    },
    "Favor": {},
    "Crafting Menus": item_zoom_rotate_button_codes,
    "Barter Menus": item_zoom_rotate_button_codes, # these were absent from controlmapvr.txt, I added them and they work just fine \o/
    "Race Sex Menu": tab_left_right_button_codes | {
        "XButton": (0x02, None), # even though it's stacked on Menu Mode, the XButton isn't really meant to be "Action A", but rather the "confirm and exit", bound to grip, so let's rebind it to the "Cancel" action
                                 # TODO: check other overrides like this in the original controlmapvr.txt and see if there's something like this that makes sense
    },
    "Dialogue Menu": menu_mode_basic_button_codes,
}

# context headers are comments that are followed by tabs and VR column hints
re_context = re.compile(r"^// (.+?)\t")
# entry columns are split by tabs or 2+ spaces (a single space is part of the event name)
re_entry_separator = re.compile(r"(\s*\t\s*|[ ]{2,})")

def main():
    in_lines = source_file.read_text(encoding="ascii").splitlines()
    out_lines = []
    current_context = None
    handled_bindings = set()
    for line_index, line in enumerate(in_lines):
        # handle input context headers (change the current context)
        context_match = re_context.match(line)
        if context_match:
            current_context = context_match.group(1)
            if current_context not in button_codes:
                raise Exception(f"Unknown input context '{current_context}'")
            out_lines.append(line)
            continue
        # handle blank and comment lines
        if not line or line.startswith("//"):
            out_lines.append(line)
            continue
        # handle binding entry line
        if current_context is None:
            raise Exception(f"Non-comment without context at line {line_index + 1}")

        entry_parts = re_entry_separator.split(line)
        validate_entry_line(entry_parts, line_index + 1)
        event_name = entry_parts[0]
        codes = button_codes[current_context].get(event_name)
        if codes is not None:
            handled_bindings.add((current_context, event_name))
        else:
            codes = (0xff, 0xff) # unbound
        right, left = codes
        if right is None: right = 0xff
        if left is None: left = 0xff

        # VR binding has 3 pairs of codes (vive/oculus/wmr right/left), unbind vive and wmr and set our oculus bindings
        for i, code in enumerate((0xff, 0xff, right, left, 0xff, 0xff)):
            # controller codes start at 4th column, but every column part is followed by the space part
            # so the indices are doubled
            entry_parts[(4 + i) * 2] = format(code, '#04x')

        line = "".join(entry_parts)
        out_lines.append(line)

    for (context, events) in button_codes.items():
        for event in events:
            if (context, event) not in handled_bindings:
                raise Exception(f"Unhandled binding {context}: {event}")

    # skyrim expects CRLF separator
    output = ("\r\n".join(out_lines) + "\r\n").encode("ascii")

    target_dir = target_file.parent
    target_dir.mkdir(parents=True, exist_ok=True)
    target_file.write_bytes(output)

def validate_entry_line(parts, line_number):
    parts_count = len(parts)
    if parts_count == 39: return # most common: 20 columns + 19 spaces between them
    if parts_count == 37: return # 20th column is optional (missing in TFC mode)
    if parts_count == 41 and parts[40] == "": return # an extra separator + empty part at the end
    raise Exception(f"Unhandled entry line of {parts_count} parts at line {line_number}")

if __name__ == "__main__":
    main()
