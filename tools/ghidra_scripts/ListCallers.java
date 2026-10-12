// Pós-script do analyzeHeadless: lista quem chama (ou referencia) cada função pedida.
//
// Argumentos: <arquivo_saida> <nome_da_funcao> [<nome_da_funcao> ...]
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class ListCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("uso: ListCallers <arquivo_saida> <funcao> ...");
            return;
        }
        try (PrintWriter w = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                // Aceita funções e também variáveis globais (qualquer símbolo com esse nome).
                for (Symbol sym : currentProgram.getSymbolTable().getGlobalSymbols(args[i])) {
                    w.println("== " + sym.getName() + " @ " + sym.getAddress());
                    for (Reference r : getReferencesTo(sym.getAddress())) {
                        Function caller = getFunctionContaining(r.getFromAddress());
                        w.println("   " + r.getReferenceType() + " de " + r.getFromAddress() + "  "
                                + (caller == null ? "(fora de função)" : caller.getName()));
                    }
                }
            }
        }
        println("ListCallers: escrito em " + args[0]);
    }
}
