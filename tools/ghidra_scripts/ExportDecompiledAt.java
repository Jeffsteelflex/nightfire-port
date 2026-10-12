// Pós-script do analyzeHeadless: decompila as funções nos endereços dados (cria a função quando o Ghidra não a achou;
// com -readOnly nada fica gravado no projeto) e grava cada uma em <saida>/<nome>_<endereço>.c
//
// Argumentos: <pasta_saida> <endereço> [<endereço> ...]   (hexadecimal, com ou sem 0x)
//@category Nightfire
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class ExportDecompiledAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("uso: ExportDecompiledAt <pasta_saida> <endereço> ...");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        int count = 0;
        for (int i = 1; i < args.length; i++) {
            String a = args[i].startsWith("0x") ? args[i].substring(2) : args[i];
            Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(a, 16));
            Function f = getFunctionAt(addr);
            if (f == null) {
                disassemble(addr);
                CreateFunctionCmd cmd = new CreateFunctionCmd(addr);
                cmd.applyTo(currentProgram, monitor);
                f = getFunctionAt(addr);
            }
            if (f == null) {
                printerr("sem função em " + addr);
                continue;
            }
            DecompileResults res = decomp.decompileFunction(f, 120, monitor);
            String code = res.decompileCompleted()
                ? res.getDecompiledFunction().getC()
                : "// falha ao decompilar: " + res.getErrorMessage() + "\n";
            String file = f.getName().replaceAll("[^A-Za-z0-9_.-]", "_") + "_" + f.getEntryPoint() + ".c";
            try (PrintWriter w = new PrintWriter(new File(outDir, file), StandardCharsets.UTF_8)) {
                w.println("// " + f.getSignature().getPrototypeString() + " @ " + f.getEntryPoint());
                w.print(code);
            }
            count++;
        }
        decomp.dispose();
        println("ExportDecompiledAt: " + count + " funções exportadas em " + outDir.getAbsolutePath());
    }
}
