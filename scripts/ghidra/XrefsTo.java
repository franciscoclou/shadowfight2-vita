// Print every reference to each address given as a script argument, with
// the function the reference sits in. Used by scripts/ghidra_game.sh xrefs.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class XrefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String a : getScriptArgs()) {
            Address addr = toAddr(a);
            println("==== xrefs to " + a);
            for (Reference r : getReferencesTo(addr)) {
                Address from = r.getFromAddress();
                Function f = getFunctionContaining(from);
                println("  " + from + " " + r.getReferenceType() + " in " +
                        (f == null ? "?" : f.getName() + " @ " + f.getEntryPoint()));
            }
        }
    }
}
