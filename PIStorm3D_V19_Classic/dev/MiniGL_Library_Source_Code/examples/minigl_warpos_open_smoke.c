/*
 * WarpOS/PPC smoke test for the existing 68k minigl.library.
 *
 * This deliberately tests only the standard AmigaOS OpenLibrary()/CloseLibrary()
 * bridge supplied by vbcc's WarpOS amiga.lib.  It does not call the MiniGL
 * dispatch LVO or any entry in its 68k dispatch table; those need a dedicated
 * PPC-to-68k proxy.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <stdio.h>

/*
 * The installed generic NDK clib/exec_protos.h expects compiler macros from
 * a 68k SDK.  amiga.lib supplies these PPC-to-68k bridge symbols, so declare
 * only the two calls this deliberately tiny smoke test needs.
 */
extern struct Library *OpenLibrary(const char *libName, ULONG version);
extern void CloseLibrary(struct Library *library);

int main(void)
{
    struct Library *miniGLBase;

#if defined(__PPC__) || defined(__powerpc__) || defined(__ppc__)
    const char *cpu = "PowerPC";
#else
    const char *cpu = "unknown (PPC predefined macro not seen)";
#endif

    printf("MiniGL WarpOS open smoke (%s)\n", cpu);

    miniGLBase = OpenLibrary("minigl.library", 10);
    if (miniGLBase == NULL) {
        printf("FAIL: could not open minigl.library version 10\n");
        return 20;
    }

    printf("OK: minigl.library opened (version %u.%u)\n",
           (unsigned int)miniGLBase->lib_Version,
           (unsigned int)miniGLBase->lib_Revision);

    CloseLibrary(miniGLBase);
    printf("OK: closed minigl.library\n");
    return 0;
}
