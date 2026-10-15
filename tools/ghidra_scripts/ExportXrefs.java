// Pós-script do analyzeHeadless: lista as referências a cada endereço pedido (dados ou código): o endereço de quem
// referencia, o tipo da referência e a função que o contém. Também acha as referências montadas por LUI + ADDIU/ORI
// (os ponteiros para cadeias e globais que o Ghidra não marcou), varrendo as instruções.
//
// Argumentos: <arquivo_saida> <endereço> [<endereço> ...]   (hexadecimal, com ou sem 0x)
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

public class ExportXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("uso: ExportXrefs <arquivo_saida> <endereço> ...");
            return;
        }
        Set<Long> wanted = new HashSet<>();
        for (int i = 1; i < args.length; i++) wanted.add(Long.parseLong(args[i].replace("0x", ""), 16));
        try (PrintWriter w = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (long a : wanted) {
                Address addr = toAddr(a);
                for (Reference r : getReferencesTo(addr)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    w.printf("%08x ref %s %s %s%n", a, r.getFromAddress(), r.getReferenceType(),
                             f == null ? "-" : f.getName(true) + "@" + f.getEntryPoint());
                }
            }
            // LUI hi; ADDIU/ORI lo no mesmo registrador, até 8 instruções depois.
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            Instruction[] window = new Instruction[8];
            int n = 0;
            while (it.hasNext()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString();
                if ((m.equals("addiu") || m.equals("ori") || m.equals("lw") || m.equals("sw") || m.equals("lwc1")
                     || m.equals("swc1") || m.equals("lq") || m.equals("sq") || m.equals("lqc2") || m.equals("sqc2"))) {
                    for (int k = 0; k < 8 && k < n; k++) {
                        Instruction lui = window[(n - 1 - k) & 7];
                        if (lui == null || !lui.getMnemonicString().equals("lui")) continue;
                        Scalar hi = lui.getScalar(1);
                        if (hi == null) continue;
                        Scalar lo = null;
                        for (int op = 0; op < ins.getNumOperands() && lo == null; op++) {
                            for (Object o : ins.getOpObjects(op))
                                if (o instanceof Scalar) lo = (Scalar) o;
                        }
                        if (lo == null) continue;
                        long v = m.equals("ori") ? ((hi.getUnsignedValue() << 16) | (lo.getUnsignedValue() & 0xffff))
                                                 : ((hi.getUnsignedValue() << 16) + lo.getSignedValue());
                        v &= 0xffffffffL;
                        if (wanted.contains(v)) {
                            Function f = getFunctionContaining(ins.getAddress());
                            w.printf("%08x hilo %s %s %s%n", v, ins.getAddress(), m,
                                     f == null ? "-" : f.getName(true) + "@" + f.getEntryPoint());
                        }
                    }
                }
                window[n & 7] = ins;
                n++;
            }
        }
        println("ExportXrefs: " + args[0]);
    }
}
