// Pós-script do analyzeHeadless: a desmontagem das funções nos endereços dados (cria a função quando o Ghidra não a
// achou; com -readOnly nada fica gravado no projeto), em <saida>/<nome>_<endereço>.s. É o par de ExportDecompiledAt
// para quando o decompilador perde argumentos ou as máscaras da VU0 (DRIVING.ELF: as chamadas com a0..a3 montados
// longe da jal, os vsub.xyz).
//
// Argumentos: <pasta_saida> <endereço> [<endereço> ...]   (hexadecimal, com ou sem 0x)
//@category Nightfire
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class ExportDisasmAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("uso: ExportDisasmAt <pasta_saida> <endereço> ...");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();
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
                printerr("sem função em " + a);
                continue;
            }
            String file = f.getName().replaceAll("[^A-Za-z0-9_.-]", "_") + "_" + f.getEntryPoint() + ".s";
            try (PrintWriter w = new PrintWriter(new File(outDir, file), StandardCharsets.UTF_8)) {
                w.println("# " + f.getName(true) + " " + f.getSignature().getPrototypeString() + " @ " + f.getEntryPoint());
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    String target = "";
                    if (ins.getFlowType().isCall()) {
                        for (Address t : ins.getFlows()) {
                            Function callee = getFunctionAt(t);
                            if (callee != null) target = "   ; " + callee.getName(true);
                        }
                    }
                    w.println(ins.getAddress() + "  " + ins.toString() + target);
                }
            }
            count++;
        }
        println("ExportDisasmAt: " + count + " funções");
    }
}
