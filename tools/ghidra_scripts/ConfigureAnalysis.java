// Pré-script do analyzeHeadless: ajusta as opções de análise para o ACTION.ELF.
// O jogo foi compilado com GCC 2.95, cujo mangling só o demangler antigo entende.
//@category Nightfire
import ghidra.app.script.GhidraScript;

public class ConfigureAnalysis extends GhidraScript {
    @Override
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "Demangler GNU.Use Deprecated Demangler", "true");
        println("Demangler GNU: Use Deprecated Demangler = true");
    }
}
