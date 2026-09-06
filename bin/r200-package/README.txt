MiniGL V19 WarpOS direct-R200 stack
==================================

This build does not use Warp3D or Warp3DPPC. The PPC DLL contains the R200
semantic frontend and sends protocol-v14 commands to the included m68k host.
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

Launch order
------------

1. Start the host asynchronously:

   Run >NIL: minigl_ppc_host

2. Start the relinked PPC application from the directory containing
   minigl_ppc.dll.

The client shuts the host down during a normal MiniGLClose(), so start the host
again before the next application run. The public service port is
MiniGLPPC.R200.Host.1.
