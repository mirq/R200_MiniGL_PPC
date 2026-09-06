#ifndef LIBRARIES_MINIGL_PPC_SEMANTIC_REPLAY_H
#define LIBRARIES_MINIGL_PPC_SEMANTIC_REPLAY_H

#include <exec/types.h>

BOOL MGLPPCSemanticCaptureBegin(ULONG frameCount);
BOOL MGLPPCSemanticCaptureEnd(void);
ULONG MGLPPCSemanticCapturedFrames(void);
BOOL MGLPPCSemanticReplayFrame(ULONG frameIndex);
void MGLPPCSemanticReplayFree(void);

#endif
