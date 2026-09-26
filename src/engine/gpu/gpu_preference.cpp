// On a laptop with two GPUs, Windows starts a process on the integrated chip
// unless the driver is told otherwise. NVIDIA (Optimus) and AMD (PowerXpress)
// decide the same way: at process start, before any window or GL context
// exists, the driver looks in the export table of the .exe for one symbol of
// its own, and a non-zero value moves the process to the discrete GPU.
//
// It has to be the .exe, which is why this file is not part of njin_rt: a
// static library only hands the linker the objects something references, and
// nothing references these two, so they would never reach an export table. The
// njin::gpu INTERFACE target compiles this file into each executable that links
// it (see CMakeLists.txt beside it).
//
// Neither vendor lets a game name a particular adapter: the request is for the
// high-performance one, and the driver picks it.

#if defined(_WIN32)
extern "C" {
// DWORD and its companion, spelled out so this file needs no windows.h.
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
