#ifndef PROTO_RADEON3D_H
#define PROTO_RADEON3D_H

/* Evergreen build: the frontend's device service is linked in
 * (backend_evergreen/library/eg_device.c), not a chip library. */
#include <exec/libraries.h>
#include <clib/radeon3d_protos.h>

extern struct Library *Radeon9200Base;     /* the frontend's library-open check */

#endif
