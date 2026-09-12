#ifndef LIBRARIES_MINIGL_PPC_IB_TRACE_H
#define LIBRARIES_MINIGL_PPC_IB_TRACE_H

/* One-shot bring-up evidence only; callers never use this in the frame loop. */
static void MGLPPCIBTrace(const char *path, const char *line)
{
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!file)
        file = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!file)
        return;
    Seek(file, 0, OFFSET_END);
    Write(file, (APTR)line, (LONG)strlen(line));
    Close(file);
}

#endif
