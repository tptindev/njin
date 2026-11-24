// The rmlui feature on a build without the SDL3/bgfx renderer (NJIN_BGFX off): RmlUi
// draws only through it, so every call reports that, and nothing is linked.
#include "njin_rml.h"
#include "njin_features.h"
#include <stdexcept>

namespace njin {
namespace {
[[noreturn]] void unavailable() { throw std::runtime_error("njin rml: RmlUi needs a build with the SDL3/bgfx renderer (NJIN_BGFX)"); }
} // namespace
struct rml_store {};
void feature_install_rmlui(context &) {}
bool rml_font_load(context &, const char *, bool) { unavailable(); }
rml_model_handle rml_model(context &, const char *) { unavailable(); }
void rml_bind_bool(context &, rml_model_handle, const char *, std::function<bool()>, std::function<void(bool)>) { unavailable(); }
void rml_bind_int(context &, rml_model_handle, const char *, std::function<i64()>, std::function<void(i64)>) { unavailable(); }
void rml_bind_float(context &, rml_model_handle, const char *, std::function<f64()>, std::function<void(f64)>) { unavailable(); }
void rml_bind_string(context &, rml_model_handle, const char *, std::function<std::string()>,
                     std::function<void(const std::string &)>) {
  unavailable();
}
void rml_bind_event(context &, rml_model_handle, const char *, std::function<void(context &)>) { unavailable(); }
rml_document_handle rml_document_load(context &, const char *) { unavailable(); }
void rml_document_show(context &, rml_document_handle, bool) { unavailable(); }
bool rml_document_visible(const context &, rml_document_handle) { return false; }
void rml_document_close(context &, rml_document_handle) {}
bool rml_on(context &, rml_document_handle, const char *, const char *, std::function<void(context &)>) { unavailable(); }
bool rml_set_inner(context &, rml_document_handle, const char *, const char *) { unavailable(); }
bool rml_wants_mouse(const context &) { return false; }
void rml_debugger(context &, bool) { unavailable(); }
} // namespace njin
