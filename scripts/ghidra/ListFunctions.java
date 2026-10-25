// Write every function of the game image as "entry end name" lines (link
// addresses, end exclusive) to the file given as the script argument. Used by
// scripts/ghidra_game.sh functions, for scripts/profile_report.py.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ListFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                w.printf("%08x %08x %s%n", f.getEntryPoint().getOffset(),
                         f.getBody().getMaxAddress().getOffset() + 1, f.getName());
            }
        }
        println("==== functions written to " + getScriptArgs()[0]);
    }
}
