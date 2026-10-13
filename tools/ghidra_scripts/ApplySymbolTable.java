// Pós-script do analyzeHeadless: aplica nomes vindos de uma tabela (tools/driving_sym.py ghidra).
//
// Argumento: <nomes.tsv>, uma linha por símbolo, separada por tabulação:
//   endereço (hex)   tipo   namespace (A::B, ou vazio)   nome   comentário
// Tipos: F = função (cria a função se o Ghidra não a achou e dá o nome no namespace),
//        C = só um comentário de cabeçalho (nome sugerido com pouca confiança; não renomeia),
//        A = acrescenta uma linha ao comentário de cabeçalho que já existe (não renomeia; tools/cxbx_names.py),
//        D = rótulo de dado (tabelas virtuais, type_info).
// Os nomes entram como SourceType.IMPORTED; o comentário de cabeçalho guarda o nome completo do SYM e a confiança.
//@category Nightfire
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

public class ApplySymbolTable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("uso: ApplySymbolTable <nomes.tsv>");
            return;
        }
        int named = 0, created = 0, comments = 0, labels = 0, failed = 0;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(args[0]), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\t", -1);
                if (p.length < 5) continue;
                Address a = toAddr(Long.parseLong(p[0], 16));
                String kind = p[1], nsPath = p[2], name = p[3], comment = p[4];
                try {
                    Namespace ns = nsPath.isEmpty() ? currentProgram.getGlobalNamespace()
                        : NamespaceUtils.createNamespaceHierarchy(nsPath, null, currentProgram, SourceType.IMPORTED);
                    if (kind.equals("F")) {
                        Function f = getFunctionAt(a);
                        if (f == null) {
                            if (getInstructionAt(a) == null) {
                                new DisassembleCommand(new AddressSet(a), null, true).applyTo(currentProgram, monitor);
                            }
                            Function inside = getFunctionContaining(a);
                            if (inside != null && !inside.getEntryPoint().equals(a)) {
                                // o Ghidra juntou duas funções: corta a de antes no novo início
                                inside.setBody(new AddressSet(inside.getEntryPoint(), a.subtract(1)));
                            }
                            f = createFunction(a, null);
                            if (f != null) created++;
                        }
                        if (f == null) { failed++; continue; }
                        f.getSymbol().setNameAndNamespace(name, ns, SourceType.IMPORTED);
                        if (!comment.isEmpty()) setPlateComment(a, comment);
                        named++;
                    } else if (kind.equals("C")) {
                        String old = getPlateComment(a);
                        if (old == null || old.isEmpty()) {
                            setPlateComment(a, comment);
                            comments++;
                        }
                    } else if (kind.equals("A")) {
                        // acrescenta uma linha ao comentário de cabeçalho (sem repetir), sem mexer no nome
                        String old = getPlateComment(a);
                        if (old == null || old.isEmpty()) {
                            setPlateComment(a, comment);
                            comments++;
                        } else if (!old.contains(comment)) {
                            setPlateComment(a, old + "\n" + comment);
                            comments++;
                        }
                    } else if (kind.equals("D")) {
                        Symbol s = createLabel(a, name, ns, true, SourceType.IMPORTED);
                        if (s != null) labels++;
                        if (!comment.isEmpty()) setPlateComment(a, comment);
                    }
                } catch (Exception e) {
                    failed++;
                    if (failed <= 20) printerr(p[0] + " " + name + ": " + e.getMessage());
                }
            }
        }
        println("ApplySymbolTable: " + named + " funções com nome (" + created + " criadas), " + comments
            + " comentários, " + labels + " rótulos, " + failed + " falhas");
    }
}
