// DataRefs.java — for each given data address, report its value, the memory
// block it lives in (and whether that block is writable), and every reference
// to it (READ vs WRITE) with the containing function.
//
// Purpose: tell a runtime-computed *global UI scale* (lives in a WRITABLE block,
// written in 1 init/resize site, READ by many layout sites) apart from a fixed
// animation constant (READ-ONLY .rdata, few readers). Decompiles the unique
// writer(s) so we can see how the value is produced.
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
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

public class DataRefs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "datarefs.txt";

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
            Memory mem = currentProgram.getMemory();
            FunctionManager fm = currentProgram.getFunctionManager();
            DecompInterface dec = new DecompInterface();
            dec.openProgram(currentProgram);
            ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
            Set<Long> writersDecompiled = new HashSet<>();

            for (int i = 1; i < a.length; i++) {
                long addr = Long.parseLong(a[i].replace("0x", ""), 16);
                Address ea = sp.getAddress(addr);
                out.println("\n==== 0x" + Long.toHexString(addr) + " ====");

                MemoryBlock blk = mem.getBlock(ea);
                int raw = 0;
                try { raw = mem.getInt(ea); } catch (Exception e) { raw = 0; }
                float f = Float.intBitsToFloat(raw);
                out.println(String.format("  value: 0x%08X  (float %.6f, int %d)", raw, f, raw));
                out.println("  block: " + (blk != null ? blk.getName() : "?")
                        + "  writable=" + (blk != null && blk.isWrite()));

                int reads = 0, writes = 0;
                Set<Long> writerEntries = new HashSet<>();
                StringBuilder refLines = new StringBuilder();
                for (Reference r : getReferencesTo(ea)) {
                    RefType rt = r.getReferenceType();
                    boolean isW = rt.isWrite();
                    boolean isR = rt.isRead();
                    if (isW) writes++;
                    if (isR) reads++;
                    Function cf = fm.getFunctionContaining(r.getFromAddress());
                    String fn = cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "?";
                    refLines.append(String.format("    %-5s %s  in %s%n",
                            isW ? "WRITE" : (isR ? "READ" : rt.getName()),
                            r.getFromAddress(), fn));
                    if (isW && cf != null) writerEntries.add(cf.getEntryPoint().getOffset());
                }
                out.println("  refs: " + reads + " read, " + writes + " write");
                out.print(refLines);

                // Decompile each distinct writer once (shows how the value is set).
                for (Long ep : writerEntries) {
                    if (!writersDecompiled.add(ep)) continue;
                    Function wf = fm.getFunctionAt(sp.getAddress(ep));
                    if (wf == null) continue;
                    out.println("  ---- WRITER " + wf.getName() + " @ " + wf.getEntryPoint()
                            + " ----");
                    DecompileResults dr = dec.decompileFunction(wf, 60, mon);
                    out.println(dr != null && dr.getDecompiledFunction() != null
                            ? dr.getDecompiledFunction().getC() : "(decompile failed)");
                }
            }
        } finally {
            out.close();
        }
    }
}
