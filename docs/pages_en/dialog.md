# Dialog and localization {#dialog}

@include dialog_i18n.cpp

## Localization

Each language is a JSON file. Nested objects have their names joined with dots:

@code{.json}
{ "_name": "English",
  "menu": { "play": "Play", "quit": "Quit" },
  "hud": { "coins": "Gold: {0}/{1}" } }
@endcode

| Function | What it does |
|---|---|
| i18n_load() | Load a language; loading another file of the same language overrides and adds to it |
| i18n_set_language() | Change the language in use (takes effect immediately) |
| i18n_languages(), i18n_language_name() | The list to show in a menu, using the `_name` key |
| tr() | The string for a key |
| trf() | Like tr(), replacing `{0}`, `{1}`... with parameters: each language reorders words on its own |

If a key is missing in the current language, it is taken from the **fallback language** (by default the first language
loaded; change it with i18n_set_fallback()); if it is missing there too, the key itself is shown and logged once, so
untranslated text shows up clearly instead of vanishing.

The engine's default font (JetBrains Mono) already has Vietnamese characters. To use another font, load it with
font_load() and put it into njin::ui_style::font and njin::dialog_style::font.

## Dialog boxes

@image html dialog_owl.png "The sample game's dialog box: portrait, speaker name (Old Owl) and text appearing gradually. Screenshot taken after the line finished"

A script is a list of linked nodes (njin::dialog_script). Write it in JSON:

@code{.json}
{ "start": "hi", "nodes": [
  { "id": "hi", "speaker": "@owl.name", "portrait": "owl", "text": "@owl.hello", "next": "ask" },
  { "id": "ask", "text": "@owl.ask",
    "choices": [ { "text": "@owl.yes", "next": "tip" },
                 { "text": "@owl.no", "event": "refused" } ] },
  { "id": "tip", "text": "@owl.tip", "event": "got_hint", "next": null } ] }
@endcode

- Text that starts with `@` is a translation key (see tr()); otherwise it is plain text.
- A node with no `next` (and no choices) continues to the node below it. `"next": null` ends it.
- `event` sends njin::dialog_event when the line appears (or when chosen); when the conversation finishes it sends njin::dialog_ended.
- `"if": "condition"` on a node or choice: the game decides with dialog_set_condition().

dialog_start() runs a script; dialog_say() says a single line (signs, objects). The dialog box is
drawn and controlled by the engine: text appears one character at a time (press once to show it all), arrows choose, Enter / Space /
button A / left mouse click moves to the next line. While it is open, those keys **do not reach the game**; movement keys
still do, so check dialog_active() or turn on `dialog_style::pause_game`.

The look is njin::dialog_style: font, font size, number of lines, text speed, the "blip" sound, portraits. The box,
name tag and choices use njin::ui_look like the UI, so 9-slice images and custom shaders work.
Register portraits with dialog_portrait().

To break a long piece of text somewhere else, use text_wrap() and draw_text_wrapped().
