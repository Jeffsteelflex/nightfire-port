// Pós-script do analyzeHeadless: decompila funções e grava cada uma em <saida>/<nome>.c
//
// Argumentos: <pasta_saida> <nome_ou_regex> [<nome_ou_regex> ...]
// Um argumento que comece com "re:" é tratado como expressão regular
// sobre o nome demangled da função. Os demais são comparados por igualdade.
//@category Nightfire
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

public class ExportDecompiled extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("uso: ExportDecompiled <pasta_saida> <nome|re:regex> ...");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();

        List<Pattern> patterns = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            patterns.add(a.startsWith("re:") ? Pattern.compile(a.substring(3)) : Pattern.compile(Pattern.quote(a)));
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            String name = f.getName();
            String qualified = f.getName(true);  // Classe::metodo (DRIVING.ELF); igual ao nome nas funções globais
            boolean match = false;
            for (Pattern p : patterns) {
                if (p.matcher(name).matches() || p.matcher(qualified).matches()) { match = true; break; }
            }
            if (!match) continue;

            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            String code = res.decompileCompleted()
                ? res.getDecompiledFunction().getC()
                : "// falha ao decompilar: " + res.getErrorMessage() + "\n";
            String file = name.replaceAll("[^A-Za-z0-9_.-]", "_") + "_" + f.getEntryPoint() + ".c";
            try (PrintWriter w = new PrintWriter(new File(outDir, file), StandardCharsets.UTF_8)) {
                w.println("// " + f.getSignature().getPrototypeString() + " @ " + f.getEntryPoint());
                w.print(code);
            }
            count++;
        }
        decomp.dispose();
        println("ExportDecompiled: " + count + " funções exportadas em " + outDir.getAbsolutePath());
    }
}
