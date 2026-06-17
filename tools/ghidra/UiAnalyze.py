# UiAnalyze.py — Ghidra headless post-script for the RCT3 UI scaling mod.
#
# Run via support/analyzeHeadless after importing the (user-provided, workspace-
# local) RCT3.exe. It decompiles the UI draw routine that issues the
# DrawIndexedPrimitive calls the frame inspector flagged, and lists its callers
# and callees so we can walk toward the widget-coordinate / quad-builder code.
#
# Call-sites came from the mod's frame inspector (RVA into RCT3.exe, image base
# 0x400000). They are passed on the command line as -scriptArgs, or default to
# the values captured on 2026-06-17. Output is plain text on stdout (captured to
# a gitignored log); record only addresses + descriptions back into ghidra_notes.
#
#@category RCT3
#@runtime Jython

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor


def parse_targets():
    args = getScriptArgs()
    if args and len(args) > 0:
        out = []
        for a in args:
            out.append(int(a, 16))
        return out
    return [0x00E2CD4D, 0x00E2CD34]  # captured UI DrawIndexedPrimitive call-sites


def main():
    space = currentProgram.getAddressFactory().getDefaultAddressSpace()
    def A(x):
        return space.getAddress(x)

    fm = currentProgram.getFunctionManager()
    listing = currentProgram.getListing()
    monitor = ConsoleTaskMonitor()
    targets = parse_targets()

    print("##### RCT3 UI ANALYSIS #####")
    print("image base: %s" % currentProgram.getImageBase())
    print("targets: %s" % ["0x%08x" % t for t in targets])

    f = fm.getFunctionContaining(A(targets[0]))
    if f is None:
        print("!! No function contains 0x%08x — auto-analysis may be incomplete." % targets[0])
        return
    print("UI_DRAW_FUNC: %s  entry=%s  end=%s" %
          (f.getName(), f.getEntryPoint(), f.getBody().getMaxAddress()))

    # Disassembly leading up to each call-site: shows how the draw args / verts
    # are set up (SetFVF, SetStreamSource, the vertex-buffer pointer, etc.).
    for t in targets:
        print("\n---- disassembly %08x-0x30 .. %08x ----" % (t, t))
        ins = listing.getInstructions(A(t - 0x30), True)
        while ins.hasNext():
            i = ins.next()
            if i.getAddress().getOffset() > t:
                break
            print("  %s  %s" % (i.getAddress(), i))

    # Full decompilation of the draw/fill routine — the heart of the UI render.
    decomp = DecompInterface()
    decomp.openProgram(currentProgram)
    print("\n==== DECOMPILE UI_DRAW_FUNC (%s) ====" % f.getEntryPoint())
    res = decomp.decompileFunction(f, 120, monitor)
    if res is not None and res.getDecompiledFunction() is not None:
        print(res.getDecompiledFunction().getC())
    else:
        print("(decompile failed)")

    # Who calls this routine — walk UP toward the per-widget layout code.
    print("\n==== CALLERS of UI_DRAW_FUNC ====")
    seen = set()
    for r in getReferencesTo(f.getEntryPoint()):
        cf = fm.getFunctionContaining(r.getFromAddress())
        if cf is None:
            continue
        key = cf.getEntryPoint().getOffset()
        if key in seen:
            continue
        seen.add(key)
        print("  caller %s  (call at %s)  name=%s" %
              (cf.getEntryPoint(), r.getFromAddress(), cf.getName()))
    print("  (%d distinct callers)" % len(seen))

    # What it calls — Lock/memcpy/vertex math live here.
    print("\n==== CALLEES of UI_DRAW_FUNC ====")
    seenc = set()
    for c in f.getCalledFunctions(monitor):
        key = c.getEntryPoint().getOffset()
        if key in seenc:
            continue
        seenc.add(key)
        print("  callee %s  name=%s" % (c.getEntryPoint(), c.getName()))


main()
