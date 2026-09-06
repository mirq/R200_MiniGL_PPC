#include <exec/types.h>
#include <stdio.h>
#include <string.h>

#include <libraries/minigl_ppc_batch.h>

static BOOL Call(MGLPPCTransport *transport, MGLPPCMessage *message,
                 ULONG command)
{
    message->command = command;
    if (!MGLPPCTransportCall(transport, message)) {
        printf("FAIL: command %lu transport failed\n", (unsigned long)command);
        return FALSE;
    }
    if (message->status != MGLPPC_STATUS_OK) {
        printf("FAIL: command %lu status %ld\n",
               (unsigned long)command, (long)message->status);
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    MGLPPCTransport transport;
    MGLPPCMessage message;
    MGLPPCBatch batch;
    ULONG contextId = 0;
    BOOL batchReady = FALSE;
    int result = 20;

    printf("MiniGLPPC R200 batched triangle smoke\n");
    if (!MGLPPCTransportOpen(&transport)) {
        printf("FAIL: cannot create PPC reply port\n");
        return 20;
    }

    memset(&message, 0, sizeof(message));
    message.value[0] = 320;
    message.value[1] = 240;
    message.value[2] = 32;
    message.value[3] = 1;
    if (!Call(&transport, &message, MGLPPC_CMD_CREATE_CONTEXT))
        goto shutdown;
    contextId = message.contextId;
    printf("OK: context %lu created\n", (unsigned long)contextId);

    if (!MGLPPCBatchInit(&batch, contextId, 256)) {
        printf("FAIL: cannot allocate triangle command batch\n");
        goto destroy;
    }
    batchReady = TRUE;
    if (!MGLPPCBatchClearColor(&batch, 0.03f, 0.05f, 0.12f, 1.0f) ||
        !MGLPPCBatchClear(&batch, MGLPPC_GL_COLOR_BUFFER_BIT) ||
        !MGLPPCBatchBeginPrimitive(&batch, MGLPPC_GL_TRIANGLES) ||
        !MGLPPCBatchColor4ub(&batch, 255, 64, 48, 255) ||
        !MGLPPCBatchVertex3f(&batch, -0.72f, -0.58f, 0.0f) ||
        !MGLPPCBatchColor4ub(&batch, 48, 224, 112, 255) ||
        !MGLPPCBatchVertex3f(&batch, 0.72f, -0.58f, 0.0f) ||
        !MGLPPCBatchColor4ub(&batch, 64, 128, 255, 255) ||
        !MGLPPCBatchVertex3f(&batch, 0.0f, 0.72f, 0.0f) ||
        !MGLPPCBatchEndPrimitive(&batch) ||
        !MGLPPCBatchPresent(&batch) ||
        !MGLPPCBatchDelay(&batch, 150)) {
        printf("FAIL: triangle command batch overflow\n");
        goto destroy;
    }

    if (!MGLPPCBatchSubmit(&transport, &batch)) {
        printf("FAIL: triangle batch transport failed\n");
        goto destroy;
    }
    if (batch.message->status != MGLPPC_STATUS_OK) {
        printf("FAIL: triangle batch status %ld, GL error %08lx\n",
               (long)batch.message->status,
               (unsigned long)batch.message->value[0]);
        goto destroy;
    }
    printf("OK: %lu-word PPC batch rendered and presented\n",
           (unsigned long)batch.message->payloadWords);
    result = 0;

destroy:
    if (batchReady)
        MGLPPCBatchDestroy(&batch);
    if (contextId != 0) {
        memset(&message, 0, sizeof(message));
        message.contextId = contextId;
        if (!Call(&transport, &message, MGLPPC_CMD_DESTROY_CONTEXT))
            result = 20;
    }

shutdown:
    memset(&message, 0, sizeof(message));
    if (!Call(&transport, &message, MGLPPC_CMD_SHUTDOWN))
        result = 20;
    MGLPPCTransportClose(&transport);
    if (result == 0)
        printf("OK: PPC-batched R200 triangle passed\n");
    return result;
}
