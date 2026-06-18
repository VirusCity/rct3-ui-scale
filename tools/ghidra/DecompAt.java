// DecompAt.java — decompile the function containing each given absolute address.
//
// Used to walk the UI render->layout chain captured at runtime by the mod's
// stack walk: pass the stack's absolute addresses and read each frame's
// decompilation to find where widget pixel coordinates are computed / a UI
// scale or base-resolution lever lives.
//
// Args: [outputFilePath] [absHex...]
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.util.task.ConsoleTaskMonitor;

public class DecompAt extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "decomp_at.txt";

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            FunctionManager fm = currentProgram.getFunctionManager();
            AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
            DecompInterface dec = new DecompInterface();
            dec.openProgram(currentProgram);
            ConsoleTaskMonitor mon = new ConsoleTaskMonitor();

            Set<Long> done = new HashSet<>();
            for (int i = 1; i < a.length; i++) {
                long addr = Long.parseLong(a[i].replace("0x", ""), 16);
                Function f = fm.getFunctionContaining(sp.getAddress(addr));
                if (f == null) {
                    out.println("\n== 0x" + Long.toHexString(addr) + ": no function ==");
                    continue;
                }
                long ep = f.getEntryPoint().getOffset();
                if (!done.add(ep)) {
                    out.println("\n== 0x" + Long.toHexString(addr) + " -> " + f.getName()
                            + " @ " + f.getEntryPoint() + " (already dumped) ==");
                    continue;
                }
                out.println("\n==== 0x" + Long.toHexString(addr) + " in " + f.getName()
                        + " @ " + f.getEntryPoint() + "  (size "
                        + f.getBody().getNumAddresses() + ") ====");
                DecompileResults dr = dec.decompileFunction(f, 90, mon);
                out.println(dr != null && dr.getDecompiledFunction() != null
                        ? dr.getDecompiledFunction().getC() : "(decompile failed)");
            }
        } finally {
            out.close();
        }
    }
}
