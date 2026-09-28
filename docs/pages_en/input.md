# Input {#input}

njin supports **keyboard**, **mouse**, **gamepad** and **text input**. There are three ways
to use them, from direct to flexible:

| Way | When to use |
|---|---|
| Ask the device directly (`key_*`, `mouse_*`, `pad_*`) | Quick, fixed things |
| **Action**: a logical name, true or false | "Jump", "fire": buttons |
| **Axis**: a logical name, a value from -1 to 1 | "Walk horizontally", "walk vertically": movement |

Actions and axes let you change keys later without touching the logic.

## Button state

The state of every device is read **once at the start of each frame** and stays the same for
the whole frame, so every system in a frame sees the same result. Every button
(key, mouse, gamepad) follows the same rule, three functions for three questions:

| Function | True when |
|---|---|
| `*_pressed` | The button **was just pressed** in this frame (one frame only) |
| `*_held` | The button was held since the previous frame and **is still held** |
| `*_released` | The button **was just released** in this frame (one frame only) |

An example of a press held for 3 frames and then released:

| Frame | Player | pressed | held | released |
|---|---|---|---|---|
| 1 | not touching | – | – | – |
| 2 | presses down | ✔ | – | – |
| 3 | still holding | – | ✔ | – |
| 4 | still holding | – | ✔ | – |
| 5 | lets go | – | – | ✔ |

@note A key pressed and released **between two frames** (an extremely fast tap) is still counted:
`*_pressed` is true in that frame and `*_released` is true in the next.

@warning In the first frame of the press (frame 2), `*_held` returns **false**. To know whether
"the button is down", use `*_pressed(...) || *_held(...)`.

## Keyboard

njin::key_pressed(), njin::key_held(), njin::key_released() take a njin::key_code:
letters, digits, F1 to F12, arrows, navigation keys, symbols, modifier keys (Shift,
Ctrl, Alt, Windows) and the numeric keypad.

## Mouse

| Function | Returns |
|---|---|
| njin::mouse_pos() | Cursor position, in screen pixels |
| njin::mouse_delta() | Movement since the previous frame |
| njin::mouse_wheel() | Number of scroll notches, positive is scrolling away from the user. Zero in most frames |
| njin::mouse_pressed() / njin::mouse_held() / njin::mouse_released() | Mouse buttons (njin::mouse_button), same rule as keys |

@note njin::mouse_pos() is in **screen** pixels, not through the camera. Convert it to a position
in the world with njin::scr2w(): see @ref camera.

## Gamepad

Gamepads are numbered from 0 to njin::gamepad_max - 1. A gamepad that is not plugged in does not cause
an error: every function reads as not pressed and returns 0.

| Function | What it does |
|---|---|
| njin::pad_available() | Whether the gamepad is plugged in |
| njin::pad_pressed() / njin::pad_held() / njin::pad_released() | Gamepad buttons (njin::gamepad_button) |
| njin::pad_axis() | Analog axis (njin::gamepad_axis), from -1 to 1 |
| njin::pad_set_deadzone() | The stick's dead zone, default 0.15 |

- Face button names go by **position**: `pad_face_down` is A on Xbox and Cross on PlayStation.
- Axis values have passed through the dead zone and been **rescaled**: just past the dead zone they read close to 0
  and reach 1 at the edge, so there is no sudden jump.
- The triggers (`pad_axis_left_trigger`, `pad_axis_right_trigger`) rest at **-1** and read 1 when
  fully pressed, following raylib's convention.

## Text input

Typing text and pressing shortcut keys are two different questions. njin::key_pressed() tells you
**which key** was pressed; njin::text_count() and njin::text_char() tell you **which character** was
typed, after the keyboard layout and dead keys. A text input field needs the second kind:

@include input_text_consume.cpp

The returned code is Unicode. Each frame receives at most 32 characters.

## Consume: swallowing input

njin::key_consume(), njin::mouse_consume() and njin::mouse_wheel_consume() clear the state
of a button for **the rest of the frame**. Systems that run after that no longer see
that button. Use them when a menu needs to swallow a click before the world underneath handles
the same click (see the example above).

The effect does not last beyond the frame that calls it, because the state is read again in the next frame. Consume
also does not falsify the button's history: a held key that gets consumed is still
"held" in the next frame, and is not counted as freshly pressed again.
To make the menu system run first, put it in an earlier phase (`phase_pre_update`) or use
the `after`/`before` ordering: see @ref modules_systems.

## Actions

An action is a logical name (for example `"fire"`) attached to one or more sources: keys, mouse
buttons, gamepad buttons. **Any source that is satisfied makes the action satisfied.** The game asks about the action
instead of the key, so changing keys later means no logic edits.

| Function | What it does |
|---|---|
| njin::action_define() | Creates an action and attaches all sources in **one line**: `action_define(ctx, "jump", {key_space, key_w, pad_face_down})`. The recommended way |
| njin::action_register() | Creates an empty action. If the name already exists it returns the existing action |
| njin::action_find() | Finds an action by name |
| njin::action_bind_key() | Attaches one more key |
| njin::action_bind_mouse() | Attaches one more mouse button |
| njin::action_bind_pad() | Attaches one more gamepad button (any gamepad that is plugged in) |
| njin::action_clear_binds() | Removes every attached source. Can be used at any time |
| njin::action_pressed() / njin::action_held() / njin::action_released() | Same rule as keys |

To change keys at runtime, call njin::action_clear_binds() and bind again.

## Axes

An axis reads a value from **-1 to 1**. It is separate from an action because the two answer two different
questions: an action is true or false, an axis is a degree.

| Function | What it does |
|---|---|
| njin::axis_define() | Creates an axis and attaches every key pair and gamepad axis in one line: `axis_define(ctx, "move", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x})`. The recommended way |
| njin::axis_register() / njin::axis_find() | Creates an empty axis, and finds an axis by name |
| njin::axis_bind_keys() | Attaches a key pair: only the negative key gives -1, only the positive key gives 1, both or neither gives 0 |
| njin::axis_bind_pad() | Attaches a gamepad axis |
| njin::axis_clear_binds() | Removes every attached source |
| njin::axis_value() | The current value |

**The source furthest from its resting position wins**: a stick pushed halfway is not flattened by a key pair
that is standing still, and a key pair being held is not blocked by a stick nobody is touching.

There is no 2-dimensional vector axis. Read two axes and combine them: only you know whether that vector
needs to be normalized (whether going diagonally should be faster).

## A complete example

Move with the keys or the left stick, fire with the space bar, left mouse button or the A button:

@include input_devices.cpp

See also a simpler action example:

@include input_actions.cpp

## List of keys and buttons

All the codes are in njin::key_code, njin::mouse_button, njin::gamepad_button and
njin::gamepad_axis.
