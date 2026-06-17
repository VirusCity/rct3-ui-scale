// UiAnalyze.java — Ghidra headless GhidraScript for the RCT3 UI scaling mod.
//
// Ghidra 12 no longer bundles Jython, so this is the Java port of the original
// analysis script (a .java GhidraScript is compiled on the fly by headless, with
// no extra extensions needed). It decompiles the UI draw routine that issues the
// DrawIndexedPrimitive calls the frame inspector flagged, and lists its callers
// and callees so we can walk toward the widget-coordinate / quad-builder code.
//
// Run via support/analyzeHeadless after the exe is imported/analyzed; reuse the
// saved project with -process -noanalysis to iterate quickly.
//
// Args: [outputFilePath] [targetHex...]. Output is written to the file so it is
// reliable regardless of how headless routes script stdout. Record only
// addresses + descriptions back into research/ghidra_notes.md (gitignored).
//
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class UiAnalyze extends GhidraScript {

    private PrintWriter out;

    private void emit(String s) {
        out.println(s);
        out.flush();
        println(s); // also to the headless log, best-effort
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args.length >= 1) ? args[0] : "ui_analysis.txt";

        long[] targets;
        if (args.length >= 2) {
            targets = new long[args.length - 1];
            for (int i = 1; i < args.length; i++) {
                targets[i - 1] = Long.parseLong(args[i].replace("0x", ""), 16);
            }
        } else {
            targets = new long[] { 0x00E2CD4DL, 0x00E2CD34L };
        }

        out = new PrintWriter(new FileWriter(outPath));
        try {
            AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
            FunctionManager fm = currentProgram.getFunctionManager();
            Listing listing = currentProgram.getListing();
            ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();

            emit("##### RCT3 UI ANALYSIS #####");
            emit("image base: " + currentProgram.getImageBase());
            StringBuilder ts = new StringBuilder("targets:");
            for (long t : targets) {
                ts.append(String.format(" 0x%08x", t));
            }
            emit(ts.toString());

            Function f = fm.getFunctionContaining(space.getAddress(targets[0]));
            if (f == null) {
                emit("!! No function contains the target — analysis may be incomplete.");
                return;
            }
            emit("UI_DRAW_FUNC: " + f.getName() + "  entry=" + f.getEntryPoint()
                    + "  end=" + f.getBody().getMaxAddress());

            // Disassembly leading up to each call-site: how the draw args / verts
            // are set up (SetFVF, SetStreamSource, the vertex-buffer pointer...).
            for (long t : targets) {
                emit("");
                emit(String.format("---- disassembly %08x-0x30 .. %08x ----", t, t));
                InstructionIterator ins = listing.getInstructions(space.getAddress(t - 0x30), true);
                while (ins.hasNext()) {
                    Instruction i = ins.next();
                    if (i.getAddress().getOffset() > t) {
                        break;
                    }
                    emit("  " + i.getAddress() + "  " + i.toString());
                }
            }

            // Full decompilation of the draw/fill routine.
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            emit("");
            emit("==== DECOMPILE UI_DRAW_FUNC (" + f.getEntryPoint() + ") ====");
            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                emit(res.getDecompiledFunction().getC());
            } else {
                emit("(decompile failed)");
            }

            // Who calls this routine — walk UP toward the per-widget layout code.
            emit("");
            emit("==== CALLERS of UI_DRAW_FUNC ====");
            Set<Long> seen = new HashSet<>();
            List<Function> callerFuncs = new ArrayList<>();
            for (Reference r : getReferencesTo(f.getEntryPoint())) {
                Function cf = fm.getFunctionContaining(r.getFromAddress());
                if (cf == null) {
                    continue;
                }
                long key = cf.getEntryPoint().getOffset();
                if (!seen.add(key)) {
                    continue;
                }
                callerFuncs.add(cf);
                emit("  caller " + cf.getEntryPoint() + "  (call at " + r.getFromAddress()
                        + ")  name=" + cf.getName());
            }
            emit("  (" + seen.size() + " distinct callers)");

            // Decompile each caller (one level up) to find the UI-specific path
            // and how the batch / vertex data is built.
            for (Function cf : callerFuncs) {
                emit("");
                emit("==== DECOMPILE CALLER " + cf.getEntryPoint() + " ("
                        + cf.getBody().getNumAddresses() + " bytes) ====");
                DecompileResults cr = decomp.decompileFunction(cf, 120, monitor);
                if (cr != null && cr.getDecompiledFunction() != null) {
                    emit(cr.getDecompiledFunction().getC());
                } else {
                    emit("(decompile failed)");
                }
            }

            // What it calls — Lock/memcpy/vertex math live here.
            emit("");
            emit("==== CALLEES of UI_DRAW_FUNC ====");
            Set<Long> seenc = new HashSet<>();
            for (Function c : f.getCalledFunctions(monitor)) {
                long key = c.getEntryPoint().getOffset();
                if (!seenc.add(key)) {
                    continue;
                }
                emit("  callee " + c.getEntryPoint() + "  name=" + c.getName());
            }
        } finally {
            out.close();
        }
    }
}
