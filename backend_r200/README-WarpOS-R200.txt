MiniGL V19 WarpOS direct-R200 stack
==================================

This build does not use Warp3D or Warp3DPPC. The PPC DLL contains the R200
semantic frontend and sends protocol-v15 commands to the included m68k host
(the host also accepts v14 clients).
The host calls the direct-R200 m68k minigl.library, which uses Radeon9200.chip.

Files
-----

minigl_ppc.dll   Hyperion WarpOS/PPC DLL; place beside the PPC application.
minigl_ppc_host  m68k R200 service; start it before the PPC application.
minigl.library   Matching direct-R200 m68k library; install in LIBS:.
libminigl.a      PPC import archive used when linking the application.

Requirements
------------

- Relink new PPC applications with this libminigl.a so MiniGLClose can detach
  explicitly. The supplied Silent Hill ABI-3 build is also supported: it
  deletes its context before its legacy MiniGLClose, allowing the DLL to
  release task-owned transport resources safely from that delete call.
- Install a matching Radeon9200.chip and Prometheus.card in LIBS:Picasso96/.
- Back up every installed library and driver before replacing it.
- Cold reboot after changing Radeon9200.chip or Prometheus.card, then run
   C:InitPPC before starting WarpOS software.

Host loader classification
--------------------------

Sonnet's legacy loader can mistake the uppercase bytes RACE in a diagnostic
string for a PPC program. The host build now checks the linked file for those
signatures and constructs the upload-trace environment name at runtime.

If replacing a previously misclassified 68k host, preserve unrelated file
protection bits and set (oldBits & ~0x00030000) | 0x00020000 with SetProtection
or the bridge's protection-bit tool. Read back both the full bits and CRC.
Never apply this override to the actual PPC DLL or PPC clients. Existing
classification bits can survive a file replacement despite a correct CRC.

The optional MGLPPC_CPEMIT=1 mode keeps command generation on the PPC and
submission/recovery on the driver-owned 68k side. It currently supports
windowed contexts only. See CP_EMITTER.md and HOST_LOADER_REGRESSION.md.

Launch order
------------

1. Start the host asynchronously:

   Run >NIL: minigl_ppc_host

2. Start the relinked PPC application from the directory containing
   minigl_ppc.dll.

The client shuts the host down during a normal MiniGLClose(), so start the host
again before the next application run. The public service port is
MiniGLPPC.R200.Host.1.
