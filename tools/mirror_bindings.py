import copy
import json
import re
import sys
from pathlib import Path

left_to_right_components = {
    "frame_controller": {
        "dpad_up": "y",
        "dpad_down": "a",
        "dpad_left": "b",
        "dpad_right": "x",
        "view": "menu",
    },
    "knuckles": {},
}

left_to_right_directional_components = {
    "frame_controller": {
        "dpad_up": "y",
        "dpad_down": "a",
        "dpad_left": "x",
        "dpad_right": "b",
        "view": "menu",
    },
    "knuckles": {},
}

directional_actions = {
    "/actions/menu/in/up",
    "/actions/menu/in/down",
    "/actions/menu/in/left",
    "/actions/menu/in/right",
    "/actions/gameplay/in/moveforward",
    "/actions/gameplay/in/moveback",
    "/actions/gameplay/in/moveleft",
    "/actions/gameplay/in/moveright",
}

kept_actions = {
    "frame_controller": {
        "/actions/menu/in/tableft",
        "/actions/menu/in/tabright",
    },
    "knuckles": set(),
}

re_hand_path = re.compile(r"^/user/hand/(left|right)/input/([a-z_]+)$")


def mirror_path(path, left_to_right, right_to_left):
    match = re_hand_path.match(path)
    if not match:
        raise Exception(f"Unhandled source path {path}")
    hand, component = match.groups()
    if hand == "left":
        return f"/user/hand/right/input/{left_to_right.get(component, component)}"
    return f"/user/hand/left/input/{right_to_left.get(component, component)}"


def source_key(source):
    return (source["path"], source.get("mode"), json.dumps(source.get("parameters", {}), sort_keys=True))


def add_source(sources, keyed, source):
    key = source_key(source)
    existing = keyed.get(key)
    if existing is None or any(input_name in existing["inputs"] for input_name in source["inputs"]):
        keyed[key] = source
        sources.append(source)
        return
    for input_name, value in source["inputs"].items():
        existing["inputs"][input_name] = value


def main():
    if len(sys.argv) != 2:
        raise Exception("Usage: mirror_bindings.py <bindings.json>")
    source_file = Path(sys.argv[1])
    target_file = source_file.with_name(source_file.stem + "_lefthanded.json")

    data = json.loads(source_file.read_text(encoding="utf-8"))
    controller_type = data["controller_type"]
    if controller_type not in left_to_right_components or controller_type not in left_to_right_directional_components or controller_type not in kept_actions:
        raise Exception(f"No component mirroring defined for controller type '{controller_type}'")
    controller_kept_actions = kept_actions[controller_type]

    left_to_right = left_to_right_components[controller_type]
    right_to_left = {v: k for k, v in left_to_right.items()}
    left_to_right_directional = left_to_right_directional_components[controller_type]
    right_to_left_directional = {v: k for k, v in left_to_right_directional.items()}

    data["name"] = data["name"] + " (left-handed)"

    for action_set in data["bindings"].values():
        new_sources = []
        keyed = {}
        for source in action_set.get("sources", []):
            kept_inputs = {}
            directional_inputs = {}
            mirrored_inputs = {}
            for input_name, value in source["inputs"].items():
                output = value["output"].lower()
                if output in controller_kept_actions:
                    kept_inputs[input_name] = value
                elif output in directional_actions:
                    directional_inputs[input_name] = value
                else:
                    mirrored_inputs[input_name] = value
            if kept_inputs:
                kept = copy.deepcopy(source)
                kept["inputs"] = kept_inputs
                add_source(new_sources, keyed, kept)
            if directional_inputs:
                moved = copy.deepcopy(source)
                moved["path"] = mirror_path(source["path"], left_to_right_directional, right_to_left_directional)
                moved["inputs"] = directional_inputs
                add_source(new_sources, keyed, moved)
            if mirrored_inputs:
                moved = copy.deepcopy(source)
                moved["path"] = mirror_path(source["path"], left_to_right, right_to_left)
                moved["inputs"] = mirrored_inputs
                add_source(new_sources, keyed, moved)
        action_set["sources"] = new_sources

    target_file.write_text(json.dumps(data, indent=3, separators=(",", " : ")) + "\n", encoding="utf-8")
    print(f"Written {target_file}")


if __name__ == "__main__":
    main()
