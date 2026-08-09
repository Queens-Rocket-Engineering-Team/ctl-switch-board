import argparse
import json
import sys
import tinyusb_keycodes

# parse the CLI arguments
parser = argparse.ArgumentParser(description="Generate switch_config C code from JSON")
parser.add_argument('config', type=str)
parser.add_argument('header', type=str)
parser.add_argument('source', type=str)
args = parser.parse_args()

# read the json file
try:
    with open(args.config, 'r', encoding='utf-8') as file:
        switch_config = json.load(file)
except Exception as e:
    print(f"Failed to read config file: {e}", file=sys.stderr)   
    sys.exit(1)

# write the header file
header_content = f"""\
#pragma once

// Generated from switch_config.json

#include "switches.h"

extern const switch_config_t switch_cfg[];
#define SWITCH_CFG_LEN {len(switch_config)}
"""

try:
    with open(args.header, 'w', encoding='utf-8') as header:
        header.write(header_content)
except Exception as e:
    print(f"Failed to create header file: {e}", file=sys.stderr)
    sys.exit(1)

# write the source file

def parse_hid_keycode(keycode_strs: list[str]) -> tuple[str, str]:
    keycodes = []
    modifiers = []

    for keycode_str in keycode_strs:
        keycode = tinyusb_keycodes.keys.get(keycode_str)
        if keycode is not None:
            if len(keycodes) >= 6: # hid only supports 6 keys pressed at once
                raise ValueError("Exceeded max of 6 key presses")
            keycodes.append(keycode)
            continue

        modifier = tinyusb_keycodes.modifiers.get(keycode_str)
        if modifier is not None:
            modifiers.append(modifier)
            continue

        raise ValueError(f"Unrecognized keycode string: {keycode_str}")

    # keycodes go into an array
    keycode_array = f"{{{', '.join(keycodes)}}}" if keycodes else "{0}"

    # hid modifiers are a bitmask
    modifier_mask = " | ".join(modifiers) if modifiers else "0"

    return keycode_array, modifier_mask

def make_switch_cfg(switch_cfg_obj: dict) -> str:
    # if rising or falling keystrokes are omitted from json or null, this will handle that cleanly
    rising_raw = switch_cfg_obj.get("rising_keystroke") or []
    falling_raw = switch_cfg_obj.get("falling_keystroke") or []

    rising_keycodes, rising_modifiers = parse_hid_keycode(rising_raw)
    falling_keycodes, falling_modifiers = parse_hid_keycode(falling_raw)

    cfg_content = f"""\
    {{
        .pin = {switch_cfg_obj["pin"]},
        .rising_keycode = {rising_keycodes},
        .rising_modifiers = {rising_modifiers},
        .falling_keycode = {falling_keycodes},
        .falling_modifiers = {falling_modifiers},
    }},"""
    return cfg_content

switches_init_code = []
for switch_cfg_obj in switch_config:
    try:
        switch_init = make_switch_cfg(switch_cfg_obj)
        switches_init_code.append(switch_init)
    except ValueError as e:
        print(f"Error in switch config for pin {switch_cfg_obj.get('pin', 'UNKNOWN')}: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyError as e:
        print(f"Missing required config key {e} for pin {switch_cfg_obj.get('pin', 'UNKNOWN')}", file=sys.stderr)
        sys.exit(1)

switches_init_code = '\n'.join(switches_init_code)

source_content = f"""\
// Generated from switch_config.json

#include <class/hid/hid.h>

#include "switches.h"
#include "switch_config.h"

const switch_config_t switch_cfg[] = {{
{switches_init_code}
}};
"""

try:
    with open(args.source, 'w', encoding='utf-8') as source:
        source.write(source_content)
except Exception as e:
    print(f"Failed to create source file: {e}", file=sys.stderr)
    sys.exit(1)