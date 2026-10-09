cgltf_write (MIT), https://github.com/jkuhlmann/cgltf

Pinned upstream commit: 85cd62382dfea638278962690cf515023f33ed00 (version string 1.15)
Vendored file: cgltf_write.h (unmodified; its MIT licence is at the end of the file).
The commit is the one whose cgltf.h raylib 6.0 bundles in src/external, so the
writer and the parser raylib compiles agree on every struct. Only the writer is
compiled here (CGLTF_WRITE_IMPLEMENTATION in rig.cpp); "cgltf.h" resolves to
raylib's copy through the target's include path.
