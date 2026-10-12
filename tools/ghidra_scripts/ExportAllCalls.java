// Pós-script do analyzeHeadless: exporta todas as funções e todas as chamadas diretas.
//
// Argumentos: <arquivo_funcoes.tsv> <arquivo_chamadas.tsv>
// funcoes.tsv:  endereço, tamanho em bytes, nome demangled, nome original
// chamadas.tsv: endereço do chamador, endereço do chamado
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

public class ExportAllCalls extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter fw = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8);
             PrintWriter cw = new PrintWriter(new File(args[1]), StandardCharsets.UTF_8)) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                String original = f.getName();
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(f.getEntryPoint())) {
                    if (s.getName().contains("__")) original = s.getName();
                }
                fw.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + f.getName(true) + "\t" + original);
                Set<String> seen = new HashSet<>();
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    for (Reference r : ins.getReferencesFrom()) {
                        if (!r.getReferenceType().isCall()) continue;
                        Function c = getFunctionAt(r.getToAddress());
                        if (c != null && seen.add(c.getEntryPoint().toString())) {
                            cw.println(f.getEntryPoint() + "\t" + c.getEntryPoint());
                        }
                    }
                }
            }
        }
        println("ExportAllCalls: ok");
    }
}
