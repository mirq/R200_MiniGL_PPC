/* Compile with each real target SDK, not host-side substitute structs. */
#include <libraries/minigl_ppc_protocol.h>

typedef char ReadbackCommandCheck[MGLPPC_CMD_READ_PIXELS_SHARED == 27UL ? 1 : -1];
typedef char ReadbackCapabilityCheck[MGLPPC_CAP_READ_PIXELS_V1 == (1UL << 10) ? 1 : -1];
typedef char ReadbackMailboxCheck[MGLPPC_MAILBOX_COMMAND_ALLOWED(MGLPPC_CMD_READ_PIXELS_SHARED) ? 1 : -1];
typedef char ReadbackWordsCheck[sizeof(((MGLPPCMessage *)0)->value) == 32 ? 1 : -1];
typedef char ReadbackMessageCheck[MGLPPC_MESSAGE_SIZE(0) == 80 ? 1 : -1];
typedef char ReadbackRingCheck[sizeof(MGLPPCRingControlV1) == 448 ? 1 : -1];
typedef char ReadbackMailboxSizeCheck[sizeof(MGLPPCMailboxV1) == 160 ? 1 : -1];

const ULONG R200ReadbackLayout[] = {
    sizeof(ULONG), MGLPPC_MESSAGE_SIZE(0), sizeof(MGLPPCRingControlV1),
    sizeof(MGLPPCMailboxV1), offsetof(MGLPPCMailboxV1, reqValue),
    offsetof(MGLPPCMailboxV1, ackValue)
};
