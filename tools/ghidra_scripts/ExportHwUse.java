// Pós-script do analyzeHeadless: mede, por função, o uso direto de hardware do PS2.
//
// Argumento: <arquivo_saida.tsv>
// Colunas: endereço, nome, instruções, COP2/VU0 em modo macro, MMI 128 bits (paddw, pextlw...),
//          acessos a registradores resolvidos (0x1000_0000-0x1200_FFFF), acessos ao scratchpad
//          (0x7000_0000) e "lui" com a metade alta de um registrador (indício fraco: pode ser constante)
//
// Registradores e scratchpad são detectados pelas referências de memória que o Ghidra resolve
// (constantes carregadas com lui/addiu); acessos por ponteiro calculado em tempo de execução
// não aparecem.
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class ExportHwUse extends GhidraScript {
    private static boolean isHw(long a) { return a >= 0x10000000L && a <= 0x1200FFFFL; }
    private static boolean isSpr(long a) { return a >= 0x70000000L && a <= 0x70003FFFL; }

    @Override
    public void run() throws Exception {
        try (PrintWriter w = new PrintWriter(new File(getScriptArgs()[0]), StandardCharsets.UTF_8)) {
            w.println("addr\tname\tinstr\tcop2\tmmi\thwreg\tspr\thwlui");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                int n = 0, cop2 = 0, mmi = 0, hw = 0, spr = 0, hwlui = 0;
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    n++;
                    String m = ins.getMnemonicString().toLowerCase();
                    if (m.startsWith("_")) m = m.substring(1);
                    if (m.equals("lqc2") || m.equals("sqc2") || m.equals("qmfc2") || m.equals("qmtc2")
                            || m.equals("ctc2") || m.equals("cfc2") || m.startsWith("vcallms") || m.startsWith("bc2")
                            || (m.startsWith("v") && m.length() > 2 && !m.equals("vor"))) {
                        cop2++;
                    } else if (m.startsWith("p") && (m.startsWith("pext") || m.startsWith("ppac") || m.startsWith("padd")
                            || m.startsWith("psub") || m.startsWith("pcpy") || m.startsWith("pmul") || m.startsWith("pmadd")
                            || m.equals("por") || m.equals("pand") || m.equals("pxor") || m.equals("pnor"))) {
                        mmi++;
                    }
                    for (Reference r : ins.getReferencesFrom()) {
                        // Só leituras e escritas de memória: constantes passadas como argumento
                        // (ex.: IDs de mensagem 0x1000xxxx dos menus) não são acesso a hardware.
                        if (!r.getReferenceType().isRead() && !r.getReferenceType().isWrite()) continue;
                        long a = r.getToAddress().getOffset();
                        if (isHw(a)) hw++;
                        if (isSpr(a)) spr++;
                    }
                    for (int op = 0; op < ins.getNumOperands(); op++) {
                        for (Object o : ins.getOpObjects(op)) {
                            if (o instanceof Scalar) {
                                long v = ((Scalar) o).getUnsignedValue();
                                // lui com a metade alta de um endereço de registrador ou do scratchpad
                                if (m.equals("lui") && (v == 0x1000 || v == 0x1001 || v == 0x1100 || v == 0x1200)) hwlui++;
                                if (m.equals("lui") && v == 0x7000) spr++;
                            }
                        }
                    }
                }
                w.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + n + "\t" + cop2 + "\t" + mmi + "\t" + hw + "\t" + spr + "\t" + hwlui);
            }
        }
        println("ExportHwUse: ok");
    }
}
