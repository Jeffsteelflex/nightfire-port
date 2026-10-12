// Pós-script do analyzeHeadless: grafo de chamadas a partir de funções raiz.
//
// Argumentos: <arquivo_saida> <profundidade> <funcao_raiz> [<funcao_raiz> ...]
// Saída: uma linha por chamada, na ordem em que aparecem no código, com indentação por nível.
// Funções já listadas aparecem com "(ver acima)" e não são expandidas de novo.
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ExportCallGraph extends GhidraScript {
    private PrintWriter out;
    private final Set<Address> seen = new HashSet<>();
    private int maxDepth;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            printerr("uso: ExportCallGraph <saida> <profundidade> <funcao> ...");
            return;
        }
        maxDepth = Integer.parseInt(args[1]);
        try (PrintWriter w = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            out = w;
            for (int i = 2; i < args.length; i++) {
                for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                    if (f.getName().equals(args[i])) walk(f, 0);
                }
            }
        }
        println("ExportCallGraph: escrito em " + args[0]);
    }

    private List<Function> callees(Function f) {
        List<Function> list = new ArrayList<>();
        Set<Address> local = new HashSet<>();
        for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            for (Reference r : ins.getReferencesFrom()) {
                if (!r.getReferenceType().isCall()) continue;
                Function c = getFunctionAt(r.getToAddress());
                if (c != null && local.add(c.getEntryPoint())) list.add(c);
            }
        }
        return list;
    }

    private void walk(Function f, int depth) {
        String pad = "  ".repeat(depth);
        boolean first = seen.add(f.getEntryPoint());
        out.println(pad + f.getName() + " @" + f.getEntryPoint() + (first ? "" : "  (ver acima)"));
        if (!first || depth >= maxDepth) return;
        for (Function c : callees(f)) walk(c, depth + 1);
    }
}
