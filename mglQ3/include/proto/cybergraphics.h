#ifndef PROTO_CYBERGRAPHICS_H
#define PROTO_CYBERGRAPHICS_H 1
#include <exec/types.h>
extern struct Library *CyberGfxBase;
#include <clib/cybergraphics_protos.h>
#if defined(__GNUC__) && defined(__PPC__) && defined(WARPUP)
#include <powerup/ppcinline/cybergraphics.h>
#else
#include <pragmas/cybergraphics_pragmas.h>
#endif

#endif /* !PROTO_CYBERGRAPHICS_H */
