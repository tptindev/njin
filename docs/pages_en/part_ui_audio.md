# Part 5: Audio and UI {#part_ui_audio}

**Level: intermediate.** Read this first: @ref part_core, and @ref input for the UI part. This part makes the game "audible" and gives it
menus and dialog boxes. The pages are independent of each other, so read whichever you need first.

## The pages

| Page | What you get |
|---|---|
| @subpage audio | Sound for short effects, music for background music, volume channels |
| @subpage ui | Menus, buttons, sliders, popups, toasts; works with mouse, keyboard and gamepad |
| @subpage ui_editor | Build menus by drag and drop in njin_ui_editor, save `.ui.json` and load it in the game |
| @subpage dialog | Dialog boxes, character-by-character text, choices, portraits; localization with `tr`/`trf` |

Suggested order: @ref ui first (every game needs menus), @ref audio, then @ref dialog if the game has characters who talk.

## What to do next

@ref part_ship, to split the game into screens, save progress and package it.
