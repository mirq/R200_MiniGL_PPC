#include "context_access.h"

/*
 * The client-visible current-context slot, reached through
 * MGLDispatchTable.currentContext.
 *
 * The R200 backend does not read this: it resolves the calling task's context
 * internally through CurrentContext(), which also enforces single-owner
 * access. The slot exists so that the SDK's mglCreateContext() inline can
 * publish the handle and the client-side wrappers can skip GL calls made with
 * no context current, exactly as they do for the other backends.
 */
GLcontext mini_CurrentContext = (GLcontext)0;
