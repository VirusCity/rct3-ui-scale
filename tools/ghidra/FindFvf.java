// FindFvf.java — locate the UI vertex setup by finding SetFVF(0x144) calls.
//
// The UI draws use FVF 0x144 (XYZRHW|DIFFUSE|TEX1); the game selects it with
// IDirect3DDevice9::SetFVF(0x144) right where it sets up / submits the UI
// vertices. This scans for `PUSH 0x144` immediates (the FVF argument) and dumps
// the containing function plus following instructions so we can spot the
// SetFVF vtable call and the surrounding vertex-buffer work.
//
// Args: [outputFilePath] [fvfHex...]   default fvf = 144, 44
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

public class FindFvf extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args.length >= 1) ? args[0] : "fvf_hits.txt";
        long[] wanted;
        if (args.length >= 2) {
            wanted = new long[args.length - 1];
            for (int i = 1; i < args.length; i++) {
                wanted[i - 1] = Long.parseLong(args[i].replace("0x", ""), 16);
            }
        } else {
            wanted = new long[] { 0x144L };
        }
        // SetFVF is IDirect3DDevice9 vtable index 89 -> byte offset 0x164. Only
        // report FVF immediates that sit next to such a call (the real selects).
        final String SETFVF = "0x164";

        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        try {
            Listing listing = currentProgram.getListing();
            FunctionManager fm = currentProgram.getFunctionManager();
            int hits = 0;
            InstructionIterator it = listing.getInstructions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction ins = it.next();
                // Only immediate operands (PUSH 0x144 / MOV reg,0x144), not
                // memory displacements like [EDX+0x144] (the draw vtable call).
                boolean immHit = false;
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    int t = ins.getOperandType(op);
                    if ((t & OperandType.SCALAR) == 0 || (t & OperandType.DYNAMIC) != 0
                            || (t & OperandType.ADDRESS) != 0) {
                        continue;
                    }
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar) {
                            long v = ((Scalar) o).getUnsignedValue();
                            for (long w : wanted) {
                                if (v == w) {
                                    immHit = true;
                                }
                            }
                        }
                    }
                }
                if (!immHit) {
                    continue;
                }
                // Require a SetFVF vtable call within a small window around it.
                boolean nearSetFvf = false;
                Instruction w = ins;
                for (int k = 0; k < 8 && w != null; k++) {
                    w = w.getNext();
                    if (w != null && w.getMnemonicString().startsWith("CALL")
                            && w.toString().contains(SETFVF)) {
                        nearSetFvf = true;
                        break;
                    }
                }
                w = ins;
                for (int k = 0; k < 8 && w != null && !nearSetFvf; k++) {
                    w = w.getPrevious();
                    if (w != null && w.getMnemonicString().startsWith("CALL")
                            && w.toString().contains(SETFVF)) {
                        nearSetFvf = true;
                    }
                }
                if (!nearSetFvf) {
                    continue;
                }
                Function f = fm.getFunctionContaining(ins.getAddress());
                out.println("SETFVF(0x144) @ " + ins.getAddress() + "  ["
                        + (f != null ? f.getName() + " " + f.getEntryPoint() : "no func")
                        + "]  " + ins);
                Instruction n = ins;
                for (int k = 0; k < 6 && n != null; k++) {
                    n = n.getNext();
                    if (n != null) {
                        out.println("      " + n.getAddress() + "  " + n);
                    }
                }
                out.println();
                hits++;
            }
            out.println("total immediate hits for wanted FVFs: " + hits);
        } finally {
            out.close();
        }
    }
}
