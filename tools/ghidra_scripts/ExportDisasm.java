// Pós-script do analyzeHeadless: exporta a desmontagem de funções (útil para ver as máscaras de
// destino das instruções da VU0, como vsub.xyz, que o decompilador omite).
//
// Argumentos: <pasta_saida> <nome|re:regex> [...]
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

public class ExportDisasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args[0]);
        outDir.mkdirs();
        List<Pattern> patterns = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String a = args[i];
            patterns.add(a.startsWith("re:") ? Pattern.compile(a.substring(3)) : Pattern.compile(Pattern.quote(a)));
        }
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            boolean match = false;
            for (Pattern p : patterns) if (p.matcher(f.getName()).matches()) match = true;
            if (!match) continue;
            String file = f.getName().replaceAll("[^A-Za-z0-9_.-]", "_") + "_" + f.getEntryPoint() + ".s";
            try (PrintWriter w = new PrintWriter(new File(outDir, file), StandardCharsets.UTF_8)) {
                w.println("# " + f.getSignature().getPrototypeString() + " @ " + f.getEntryPoint());
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    w.println(ins.getAddress() + "  " + ins.toString());
                }
            }
            count++;
        }
        println("ExportDisasm: " + count + " funções");
    }
}
