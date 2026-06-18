// XrefTo.java — list and decompile the callers of a named (imported) function.
//
// Used to understand how RCT3 consumes an API. For the UI-scaling mod we care
// about GetCursorPos: is its result used only for UI hit-testing (safe to
// transform globally) or also for 3D world picking (would break if transformed)?
//
// Args: [outputFilePath] [symbolName]   default symbol = GetCursorPos
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.ConsoleTaskMonitor;

public class XrefTo extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "xref.txt";
        String name = (a.length >= 2) ? a[1] : "GetCursorPos";

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            FunctionManager fm = currentProgram.getFunctionManager();
            SymbolTable st = currentProgram.getSymbolTable();
            DecompInterface dec = new DecompInterface();
            dec.openProgram(currentProgram);
            ConsoleTaskMonitor mon = new ConsoleTaskMonitor();

            Set<Long> callerEntries = new HashSet<>();
            int refCount = 0;
            for (Symbol sym : st.getSymbols(name)) {
                out.println("symbol " + name + " @ " + sym.getAddress()
                        + " type=" + sym.getSymbolType());
                for (Reference r : getReferencesTo(sym.getAddress())) {
                    refCount++;
                    Function cf = fm.getFunctionContaining(r.getFromAddress());
                    out.println("  ref from " + r.getFromAddress() + "  in "
                            + (cf != null ? cf.getName() + " " + cf.getEntryPoint() : "?"));
                    if (cf != null) {
                        callerEntries.add(cf.getEntryPoint().getOffset());
                    }
                }
            }
            out.println("total refs: " + refCount + ", distinct callers: "
                    + callerEntries.size());

            for (Long ep : callerEntries) {
                Address a2 = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(ep);
                Function cf = fm.getFunctionAt(a2);
                if (cf == null) {
                    continue;
                }
                out.println("");
                out.println("==== DECOMP " + cf.getEntryPoint() + " (" + cf.getName()
                        + ") ====");
                DecompileResults dr = dec.decompileFunction(cf, 90, mon);
                out.println(dr != null && dr.getDecompiledFunction() != null
                        ? dr.getDecompiledFunction().getC() : "(decompile failed)");
            }
        } finally {
            out.close();
        }
    }
}
