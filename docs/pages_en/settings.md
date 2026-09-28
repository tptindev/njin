# Player settings {#settings}

Volume, keys, fullscreen, language: the things players adjust themselves and expect the game to remember.

@include settings_rebind.cpp

## Audio channels

njin::audio_bus splits sound into channels, like the sliders in a settings menu. A sound's real volume is
its own volume, times the channel volume, times the `bus_master` channel.

| Channel | Used for |
|---|---|
| `bus_master` | Multiplied into everything |
| `bus_music` | All music |
| `bus_sfx` | In-game effects (the default for a sound) |
| `bus_ui` | Menu sounds; put them in with sound_set_bus() |
| `bus_voice` | Voice-over |

audio_set_bus_volume() and audio_set_bus_muted() take effect immediately, even on sounds already playing.
music_crossfade() switches music smoothly (on real time, so it runs even while the game is paused);
music_fade_in() and music_fade_out() work on a single track.

## Key rebinding

An action can be bound to a key, a mouse button and a gamepad button. To let the player change them:

- **ui_keybind()**: one row in the menu, "Jump | Space". Click it, then press the new key (Esc cancels). Use
  `ui_keybind(ctx, "Jump##pad", action, true)` for the gamepad button row. While waiting, the UI does not
  navigate; check ui_keybind_listening() so you do not treat Esc as "close the menu".
- action_rebind() is the rebinding operation underneath: the new source replaces the action's source **on the same device**, and is
  removed from every other action so one key never does two jobs. Get the key with input_any_pressed().
- input_source_name() gives the display text ("Space", "Left Shift", "Pad A"), and action_sources() lists the keys
  currently bound: use them to draw a hint like "Press E to talk" that matches the key the player has rebound.

## Saving and loading

@code
njin::settings_load(ctx); // after registering all actions and axes and loading languages
njin::settings_save(ctx); // when leaving the settings menu
@endcode

The file is JSON in the save-game folder (`settings.json`, see save_path()): each channel's volume and mute, the keys of every
action and axis, fullscreen, language. Anything not in the file is left
as it is, so a game that adds a new action in an update still has the default key. The game's own data
(difficulty, brightness) comes along under the `game` key: `settings_save(ctx, "settings.json", &data)`.

If you need to save the keys somewhere else yourself: input_bindings_save() and input_bindings_load().

## Gamepad rumble

pad_rumble() drives the two motors (low and fast) for a duration; a gamepad with no motors
does nothing. Combine it with camera_shake() and hitstop() when a hit lands.
