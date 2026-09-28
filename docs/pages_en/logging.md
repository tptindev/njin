# Logging {#logging}

njin has its own logger, used through the `NJIN_*` macros, which are formatted like `printf`:

```cpp
NJIN_INFO("player spawned at %.1f, %.1f", pos.x, pos.y);
NJIN_WARN("missing texture: %s", path);
NJIN_ERROR("error code %d", code);
```

Each line prints in this form:

```
[  1.234] WARN  file.cpp:42  missing texture: assets/player.png
```

made of the time (seconds), the level, the calling file and line, and the message. Logs from the window/audio
backend also go through this logger, tagged `njin` like the engine's own logs:
njin hides it from the game's code (see [Architecture](#architecture)), so the log does not
name it either.

## Levels

From most detailed to most serious (njin::log_level):

| Macro | Level | Use it when |
|---|---|---|
| NJIN_TRACE | njin::log_trace | Tracing a bug in detail |
| NJIN_DEBUG | njin::log_debug | Debugging information |
| NJIN_INFO | njin::log_info | Normal events |
| NJIN_WARN | njin::log_warn | There is a problem but it keeps running |
| NJIN_ERROR | njin::log_error | One operation failed |
| NJIN_FATAL | njin::log_fatal | It cannot continue. **Stops the program** |

## Filtering and redirecting

- njin::log_set_level() drops every line below a level. The default is
  `log_debug` in a debug build and `log_info` when `NDEBUG` is defined.
- njin::log_set_sink() replaces where the log goes (the default is stderr), for example to write to a file.
- When njin_inspector is connected to the game (see @ref debug), the log shows up in the inspector instead of stderr.
  With no inspector, the log goes to stderr as usual. A sink the game set with
  njin::log_set_sink() still receives the log as before.

@include logging.cpp

@note `line` is `0` when the exact line is not known and only a source name is (`file` is
`"njin"` for logs from the window/audio backend); `file` is also `nullptr` when
nothing is known.

The full list of functions and macros is in the group @ref grp_log.
