#pragma once
#include "document.h"
namespace model_editor {
// Draws the same ordered SDF as Field directly, without re-meshing animated poses.
class LivePreview {
public:
  bool initialize();
  void shutdown();
  bool valid() const { return shader.id != 0; }
  void draw(const Field &field, const Camera3D &camera, int width, int height, Vector3 color);
private:
  Shader shader{};
};
}
