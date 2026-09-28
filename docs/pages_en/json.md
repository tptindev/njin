# JSON and saving the game {#json}

njin::json_value reads and writes JSON: save games, config files, your game's data tables. It is also
the type of the properties read from Tiled and LDtk (see @ref level).

@include save_json.cpp

## Reading

- `doc["key"]` and `doc[i]` never fail: a missing key, a wrong type or an index past the end of an
  array all return a null value. Reading as deep as you like is safe: `doc["a"]["b"]["c"]`.
- Get a value with a fallback: `int_or()`, `f32_or()`, `number_or()`, `bool_or()`, `string_or()`.
  This way an old save that lacks a new key still loads.
- Iterate: arrays through `items`, objects through `members` (kept in the order of the file).
- `is()`, `has()`, `size()` to check.

## Writing

- `json_value::make_object()` and `json_value::make_array()` create empty values.
- `set(key, value)` sets a member (replaces it if present, keeping its position), `push(value)` appends
  to an array. Both return the value itself, so calls can be chained.
- Integers are written without a decimal part; floating-point numbers are written as short as they can be
  while still reading back exactly (`0.1`, not `0.10000000000000001`).

## Files

| Function | What it does |
|---|---|
| njin::json_load() | Reads and parses a file. Errors are logged with the byte position |
| njin::json_save() | Writes through a temporary file, then renames it: a power cut halfway does not corrupt the old copy |
| njin::json_parse() | Parses a string |
| njin::json_dump() | Writes out a string, indented or on one line |

Save games belong in the user's own folder: njin::save_path() (see @ref window_files).
