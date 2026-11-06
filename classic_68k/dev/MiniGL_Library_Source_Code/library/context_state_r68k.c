/* minigl68k.library: the client-visible current-context slot (see
 * context_state_r200.c); built against the ABI-5 SDK headers. */
#include <exec/types.h>
#include <libraries/minigl_dispatch.h>

GLcontext mini_CurrentContext = (GLcontext)0;
