#pragma once
#include "_types.h"
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_i18n
/// @{

/// Loads the string table of a language from a JSON file.
///
/// The file is an object; nested objects have their names joined with a dot, so the
/// following two forms are the same:
/// @code{.json}
/// { "menu": { "play": "Play", "quit": "Quit" }, "_name": "English" }
/// { "menu.play": "Play", "menu.quit": "Quit", "_name": "English" }
/// @endcode
/// The `_name` key is the language name to show in menus (i18n_language_name()).
/// Loading the same language again overwrites the old strings with the new ones, so a table
/// can be split across several files. The first language loaded becomes the current language.
/// @param ctx Engine context.
/// @param lang Language code, for example `"vi"`, `"en"`.
/// @param path JSON file path.
/// @return `false` if the file cannot be read (logged).
bool i18n_load(context &ctx, const char *lang, const char *path);

/// Selects the current language. A language that has not been loaded is ignored (logged).
/// @param ctx Engine context.
/// @param lang Language code.
void i18n_set_language(context &ctx, const char *lang);

/// Code of the current language, or an empty string if no language has been loaded.
/// @param ctx Engine context. @return Language code.
const char *i18n_language(const context &ctx);

/// Fallback language: when the current language is missing a key, it is looked up here
/// (usually the game's source language). Defaults to the first language loaded.
/// @param ctx Engine context.
/// @param lang Language code.
void i18n_set_fallback(context &ctx, const char *lang);

/// Codes of the loaded languages, in load order. Use it for the language choice in the
/// settings menu.
/// @param ctx Engine context. @return List of codes.
std::vector<std::string> i18n_languages(const context &ctx);

/// Display name of a language: the `_name` key in its file, or the code itself.
/// @param ctx Engine context. @param lang Language code. @return Name.
const char *i18n_language_name(const context &ctx, const char *lang);

/// Translated string of `key` in the current language.
///
/// If missing it is taken from the fallback language; if still missing it returns `key` itself (and logs
/// once), so untranslated text shows up clearly instead of vanishing.
/// @code
/// njin::ui_button(ctx, njin::tr(ctx, "menu.play"));
/// @endcode
/// The returned string stays valid until more files are loaded or the language changes.
/// @param ctx Engine context.
/// @param key Key.
/// @return UTF-8 string.
const char *tr(const context &ctx, const char *key);

/// Like tr(), then replaces `{0}`, `{1}`... with the arguments in order: a translated sentence
/// can reorder words per language.
/// @code
/// // "hud.coins": "Gold: {0}/{1}"
/// njin::trf(ctx, "hud.coins", {std::to_string(coins), std::to_string(total)});
/// @endcode
/// @param ctx Engine context.
/// @param key Key.
/// @param args Values to substitute.
/// @return The string after substitution.
std::string trf(const context &ctx, const char *key, std::initializer_list<std::string_view> args);

/// Whether this key exists in the current language or the fallback language.
/// @param ctx Engine context. @param key Key. @return `true` if it exists.
bool i18n_has(const context &ctx, const char *key);
/// @}
} // namespace njin
