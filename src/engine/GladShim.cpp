// AlloLib's al_OpenGL.cpp is still linked (something in the engine references a
// helper from it), and it names one glad entry point. With AlloLib's graphics
// layer compiled out and its glad removed from the link, that single symbol is
// undefined. Nothing calls it in a plugin, so a null function pointer satisfies
// the linker without pulling glad back in and colliding with iPlug2's copy.

extern "C" {
using PFNGLCLEARBUFFERFVPROC_LOCAL = void (*)(unsigned int, int, const float *);
PFNGLCLEARBUFFERFVPROC_LOCAL glad_glClearBufferfv = nullptr;
}
