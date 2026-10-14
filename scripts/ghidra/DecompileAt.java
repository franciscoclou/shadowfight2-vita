// Decompile the function containing each address given as a script
// argument and print its C. Used by scripts/ghidra_game.sh.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        for (String a : getScriptArgs()) {
            Address addr = toAddr(a);
            Function f = getFunctionContaining(addr);
            if (f == null) {
                disassemble(addr);
                f = createFunction(addr, null);
            }
            println("==== " + a + " -> " + (f == null ? "no function" : f.getName() + " @ " + f.getEntryPoint()));
            if (f == null)
                continue;
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                           : "decompile failed: " + r.getErrorMessage());
        }
    }
}
