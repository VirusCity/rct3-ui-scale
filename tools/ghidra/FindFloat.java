// FindFloat.java — find referenced float constants within a value range.
//
// Used to locate RCT3's per-element UI-scale clamp (max ~1.46): a rare float in
// .rdata that code loads. Reports each matching float that has code references,
// plus the referencing function — i.e. the scale-handling code to patch.
//
// Args: [outputFilePath] [lo] [hi]   default range 1.40 .. 1.50
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class FindFloat extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "floats.txt";
        float lo = (a.length >= 3) ? Float.parseFloat(a[1]) : 1.40f;
        float hi = (a.length >= 3) ? Float.parseFloat(a[2]) : 1.50f;

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            Memory mem = currentProgram.getMemory();
            FunctionManager fm = currentProgram.getFunctionManager();
            out.println("float constants in [" + lo + ", " + hi + "] with code refs:");
            int hits = 0;
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isInitialized() || b.isExecute()) {
                    continue;  // data blocks only
                }
                long size = b.getSize();
                for (long off = 0; off + 4 <= size && !monitor.isCancelled(); off += 4) {
                    Address addr = b.getStart().add(off);
                    int bits = mem.getInt(addr);
                    float f = Float.intBitsToFloat(bits);
                    if (!(f >= lo && f <= hi)) {
                        continue;
                    }
                    Reference[] refs = getReferencesTo(addr);
                    if (refs.length == 0) {
                        continue;
                    }
                    StringBuilder sb = new StringBuilder();
                    for (Reference r : refs) {
                        Function cf = fm.getFunctionContaining(r.getFromAddress());
                        sb.append("\n    <- ").append(r.getFromAddress());
                        if (cf != null) {
                            sb.append("  ").append(cf.getName()).append(" @ ")
                              .append(cf.getEntryPoint());
                        }
                    }
                    out.println(String.format("%s  f=%.6f (0x%08X)  %d ref(s):%s",
                            addr, f, bits, refs.length, sb.toString()));
                    hits++;
                }
            }
            out.println("total: " + hits);
        } finally {
            out.close();
        }
    }
}
