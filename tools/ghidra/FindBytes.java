// FindBytes.java — find a raw byte sequence and report containing functions.
//
// For the UI source patch: locate `mov dword [reg+0xF0], 1.0f` (the GUI2 element
// scale default), i.e. the displacement+immediate sequence F0 00 00 00 00 00 80
// 3F. Reports each hit with the containing function so we can pick the element
// base constructor to patch.
//
// Args: [outputFilePath] [hexBytes]   default hex = F0000000000080 3F (no spaces)
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class FindBytes extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "bytes.txt";
        String hex = (a.length >= 2) ? a[1] : "F0000000000080" + "3F";
        hex = hex.replace(" ", "");
        int n = hex.length() / 2;
        byte[] pat = new byte[n];
        for (int i = 0; i < n; i++) {
            pat[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
        }

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            Memory mem = currentProgram.getMemory();
            FunctionManager fm = currentProgram.getFunctionManager();
            out.println("searching for bytes: " + hex);
            int hits = 0;
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isInitialized()) {
                    continue;
                }
                Address found = mem.findBytes(b.getStart(), b.getEnd(), pat, null, true,
                        monitor);
                while (found != null) {
                    // The mov opcode (C7 /0) starts 2 bytes before the disp32.
                    Address instr = found.subtract(2);
                    Function f = fm.getFunctionContaining(found);
                    out.println(String.format("hit @ %s  (mov likely @ %s)  in %s", found,
                            instr, f != null ? f.getName() + " @ " + f.getEntryPoint()
                                             : "<no function>"));
                    hits++;
                    Address next = found.add(1);
                    if (next.compareTo(b.getEnd()) >= 0) {
                        break;
                    }
                    found = mem.findBytes(next, b.getEnd(), pat, null, true, monitor);
                }
            }
            out.println("total hits: " + hits);
        } finally {
            out.close();
        }
    }
}
