// FindOffsetReads.java — find FLOAT instructions that access a struct offset.
//
// For the UI source patch: locate where the GUI2 layout READS element+0xF0 (the
// per-element scale) as a float and uses it. Scans every instruction for a
// memory operand [reg + DISP] with a float-ish mnemonic (FPU/SSE) and reports
// the address, instruction, and containing function. A function that reads BOTH
// +0xEC and +0xF0 (the paired scale) is the prime layout/scale-apply candidate.
//
// Args: [outputFilePath] [dispHex]   default disp = F0
//@category RCT3

import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.scalar.Scalar;

public class FindOffsetReads extends GhidraScript {

    private static boolean isFloatMnem(String m) {
        if (m.length() > 0 && m.charAt(0) == 'F') return true;  // FPU: FLD/FMUL/...
        return m.endsWith("SS") || m.endsWith("SD") || m.endsWith("PS")
                || m.endsWith("PD") || m.startsWith("CVT");      // SSE scalar/packed
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outPath = (a.length >= 1) ? a[0] : "offreads.txt";
        long disp = (a.length >= 2) ? Long.parseLong(a[1].replace("0x", ""), 16) : 0xF0;

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            Listing listing = currentProgram.getListing();
            FunctionManager fm = currentProgram.getFunctionManager();
            int hits = 0;
            InstructionIterator it = listing.getInstructions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction ins = it.next();
                if (!isFloatMnem(ins.getMnemonicString())) continue;
                int opIdx = -1;
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    int t = ins.getOperandType(op);
                    if ((t & OperandType.DYNAMIC) == 0) continue;  // memory operand only
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar
                                && ((Scalar) o).getUnsignedValue() == disp) {
                            opIdx = op;
                        }
                    }
                }
                if (opIdx < 0) continue;
                Function f = fm.getFunctionContaining(ins.getAddress());
                out.println(String.format("%s  op%d  %-30s  %s", ins.getAddress(),
                        opIdx, ins.toString(),
                        f != null ? f.getName() + " @ " + f.getEntryPoint() : "<none>"));
                hits++;
            }
            out.println("total float [reg+0x" + Long.toHexString(disp) + "] accesses: "
                    + hits);
        } finally {
            out.close();
        }
    }
}
