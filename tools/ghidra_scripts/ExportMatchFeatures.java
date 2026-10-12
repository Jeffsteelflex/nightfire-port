// Pós-script do analyzeHeadless: exporta o que tools/xbox_match.py usa para casar funções entre duas compilações do
// mesmo código-fonte em processadores diferentes (o DRIVING.ELF do PS2, MIPS, e o Driving.xbe do Xbox, x86).
//
// Argumento: <saida.tsv>. Uma linha por fato, separada por tabulação, agrupadas por função:
//   F  entrada  tamanho  nome_qualificado  blocos  instruções
//   C  endereço_chamado                  chamada direta, na ordem do código (com repetição)
//   S  texto                             cadeia lida pela função (com \t \n \\ escapados)
//   D  endereço  tamanho                 dado lido ou apontado que não é cadeia (tamanho 4/8 se o operando diz, ou 0)
//   K  valor                             constante inteira de um operando (escalar), em hex
//   X  valor                             constante float montada em registrador (MIPS: lui [+ ori] -> mtc1), em hex
//   V  endereço                          endereço de código gravado como dado (ponteiro de tabela virtual etc.)
// e, no fim, as tabelas de ponteiros para código fora das funções:
//   T  endereço  alvo1,alvo2,...         sequência de 2+ ponteiros para início de função num bloco de dados
//@category Nightfire
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.block.BasicBlockModel;
import ghidra.program.model.block.CodeBlockIterator;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ExportMatchFeatures extends GhidraScript {
    private Memory mem;
    private boolean mips;

    private static String esc(String s) {
        return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
    }

    /** Uma cadeia ASCII de 3+ caracteres imprimíveis terminada em zero no endereço, ou null. */
    private String cstring(Address a) {
        try {
            MemoryBlock b = mem.getBlock(a);
            if (b == null || !b.isInitialized() || b.isExecute()) return null;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 512; i++) {
                int c = mem.getByte(a.add(i)) & 0xff;
                if (c == 0) return sb.length() >= 3 ? sb.toString() : null;
                if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127)) sb.append((char) c);
                else return null;
            }
        } catch (Exception e) {
            return null;
        }
        return null;
    }

    private boolean referencedByCode(Address a) {
        for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
            MemoryBlock b = mem.getBlock(r.getFromAddress());
            if (b != null && b.isExecute()) return true;
        }
        return false;
    }

    private boolean isCode(Address a) {
        MemoryBlock b = mem.getBlock(a);
        return b != null && b.isExecute() && getFunctionAt(a) != null;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        mem = currentProgram.getMemory();
        mips = currentProgram.getLanguage().getProcessor().toString().toLowerCase().contains("mips");
        BasicBlockModel bbm = new BasicBlockModel(currentProgram);
        int nf = 0;
        try (PrintWriter w = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.isThunk() || f.isExternal()) continue;
                int blocks = 0;
                CodeBlockIterator it = bbm.getCodeBlocksContaining(f.getBody(), monitor);
                while (it.hasNext()) { it.next(); blocks++; }
                List<String> lines = new ArrayList<>();
                Map<Integer, Long> hi = new HashMap<>();   // MIPS: registrador -> valor de um lui [+ ori] recente
                int count = 0;
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    count++;
                    String mn = ins.getMnemonicString().toLowerCase();
                    if (mn.startsWith("_")) mn = mn.substring(1);
                    java.util.Set<Long> refd = new java.util.HashSet<>();
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (!to.isMemoryAddress()) continue;
                        RefType t = r.getReferenceType();
                        if (t.isCall()) {
                            Function c = getFunctionAt(to);
                            if (c != null) lines.add("C\t" + c.getEntryPoint());
                            continue;
                        }
                        if (t.isFlow()) continue;
                        refd.add(to.getOffset());
                        String s = cstring(to);
                        if (s != null) { lines.add("S\t" + esc(s)); continue; }
                        if (isCode(to)) { lines.add("V\t" + to); continue; }
                        int size = 0;
                        for (int op = 0; op < ins.getNumOperands(); op++) {
                            String rep = ins.getDefaultOperandRepresentation(op).toLowerCase();
                            if (rep.contains("qword ptr")) size = 8;
                            else if (rep.contains("dword ptr")) size = 4;
                        }
                        if (mips && (mn.equals("lwc1") || mn.equals("lw"))) size = 4;
                        lines.add("D\t" + to + "\t" + size);
                    }
                    for (int op = 0; op < ins.getNumOperands(); op++) {
                        for (Object o : ins.getOpObjects(op)) {
                            if (!(o instanceof Scalar)) continue;
                            long v = ((Scalar) o).getUnsignedValue() & 0xffffffffL;
                            if (refd.contains(v)) continue;
                            if (mips && mn.equals("lui")) {
                                // o valor que interessa é o registrador montado (ver X), não o imediato do lui
                                continue;
                            }
                            if (v > 0x10 && v < 0xffffff00L) lines.add("K\t" + Long.toHexString(v));
                        }
                    }
                    if (mips) {
                        // lui rX,hi ; [ori rX,rX,lo] ; mtc1 rX,fY  ->  a constante float (hi << 16 | lo)
                        Object[] o0 = ins.getNumOperands() > 0 ? ins.getOpObjects(0) : new Object[0];
                        if (mn.equals("lui") && o0.length == 1 && o0[0] instanceof ghidra.program.model.lang.Register) {
                            Object[] o1 = ins.getOpObjects(1);
                            if (o1.length == 1 && o1[0] instanceof Scalar) {
                                hi.put(((ghidra.program.model.lang.Register) o0[0]).getOffset(),
                                    (((Scalar) o1[0]).getUnsignedValue() & 0xffff) << 16);
                            }
                        } else if (mn.equals("ori") && ins.getNumOperands() == 3) {
                            Object[] a0 = ins.getOpObjects(0), a1 = ins.getOpObjects(1), a2 = ins.getOpObjects(2);
                            if (a0.length == 1 && a1.length == 1 && a2.length == 1
                                && a0[0] instanceof ghidra.program.model.lang.Register
                                && a1[0] instanceof ghidra.program.model.lang.Register && a2[0] instanceof Scalar) {
                                int src = ((ghidra.program.model.lang.Register) a1[0]).getOffset();
                                int dst = ((ghidra.program.model.lang.Register) a0[0]).getOffset();
                                Long v = hi.get(src);
                                if (v != null && src == dst) hi.put(dst, v | (((Scalar) a2[0]).getUnsignedValue() & 0xffff));
                            }
                        } else if (mn.equals("mtc1") && ins.getNumOperands() >= 1) {
                            for (Object o : o0) {
                                if (o instanceof ghidra.program.model.lang.Register) {
                                    Long v = hi.get(((ghidra.program.model.lang.Register) o).getOffset());
                                    if (v != null) lines.add("X\t" + Long.toHexString(v));
                                }
                            }
                        }
                        if (ins.getFlowType().isJump() || ins.getFlowType().isCall()) hi.clear();
                    }
                }
                w.println("F\t" + f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t"
                    + esc(f.getName(true)) + "\t" + blocks + "\t" + count);
                for (String l : lines) w.println(l);
                nf++;
            }
            // Tabelas de ponteiros para funções nos blocos de dados (tabelas virtuais, de despacho).
            int nt = 0;
            int ptr = currentProgram.getDefaultPointerSize();
            for (MemoryBlock b : mem.getBlocks()) {
                if (!b.isInitialized() || b.isExecute()) continue;
                Address a = b.getStart();
                Address end = b.getEnd();
                long align = a.getOffset() % 4;
                if (align != 0) a = a.add(4 - align);
                List<String> run = new ArrayList<>();
                Address runStart = null;
                while (a.compareTo(end) < 0 && a.add(3).compareTo(end) <= 0) {
                    long v = mem.getInt(a) & 0xffffffffL;
                    Address to = null;
                    try { to = toAddr(v); } catch (Exception e) { to = null; }
                    boolean code = v != 0 && to != null && mem.contains(to) && isCode(to);
                    // GCC 2.95 (PS2): entradas de 8 bytes {s16 delta, s16 índice, ponteiro}; aceita as palavras pequenas
                    // entre os ponteiros quando o programa é MIPS.
                    // MSVC sem RTTI: tabelas encostadas; uma nova começa onde o código aponta (o construtor a grava).
                    if (code && runStart != null && !mips && referencedByCode(a)) {
                        if (run.size() >= 2 || referencedByCode(runStart)) {
                            w.println("T\t" + runStart + "\t" + String.join(",", run));
                            nt++;
                        }
                        run.clear();
                        runStart = null;
                    }
                    if (code) {
                        if (runStart == null) runStart = a;
                        run.add(to.toString());
                    } else if (!(mips && runStart != null && v < 0x10000)) {
                        if (run.size() >= 2 || (runStart != null && !mips && referencedByCode(runStart))) {
                            w.println("T\t" + runStart + "\t" + String.join(",", run));
                            nt++;
                        }
                        run.clear();
                        runStart = null;
                    }
                    a = a.add(4);
                }
                if (run.size() >= 2 || (runStart != null && !mips && referencedByCode(runStart))) {
                    w.println("T\t" + runStart + "\t" + String.join(",", run));
                    nt++;
                }
            }
            println("ExportMatchFeatures: " + nf + " funções, " + nt + " tabelas de ponteiros");
        }
    }
}
