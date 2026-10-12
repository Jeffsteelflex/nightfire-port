// Pós-script do analyzeHeadless para o Driving.xbe convertido em PE (tools/ghidra_xbox.sh import): as tabelas
// virtuais.
//
// O jogo de Xbox foi compilado pelo MSVC sem RTTI: uma tabela virtual é só uma sequência de ponteiros para código no
// .rdata, encostada na próxima. A análise automática do Ghidra não cria funções nos alvos que nada chama diretamente
// (metade dos métodos virtuais). Este script acha as sequências de 2+ ponteiros para o .text no .rdata, corta-as onde o
// código grava o endereço (o construtor: mov dword ptr [reg], offset tabela), dá a cada pedaço o rótulo
// vtable_<endereço>, cria as funções que faltam nos alvos e reanalisa.
//@category Nightfire
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class XboxVtables extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock text = mem.getBlock(".text");
        MemoryBlock rdata = mem.getBlock(".rdata");
        if (text == null || rdata == null) {
            printerr("XboxVtables: faltam os blocos .text e .rdata");
            return;
        }
        // Endereços do .rdata que o código grava ou lê como imediato (início de tabela).
        Set<Long> written = new HashSet<>();
        for (Instruction ins : currentProgram.getListing().getInstructions(text.getStart(), true)) {
            if (!text.contains(ins.getAddress())) break;
            for (Reference r : ins.getReferencesFrom()) {
                Address to = r.getToAddress();
                if (rdata.contains(to)) written.add(to.getOffset());
            }
        }
        List<Address[]> tables = new ArrayList<>();
        List<Address> run = new ArrayList<>();
        Address a = rdata.getStart();
        long mis = a.getOffset() % 4;
        if (mis != 0) a = a.add(4 - mis);
        while (a.add(3).compareTo(rdata.getEnd()) <= 0) {
            long v = mem.getInt(a) & 0xffffffffL;
            boolean code = false;
            try {
                Address to = toAddr(v);
                code = text.contains(to);
            } catch (Exception e) {
                code = false;
            }
            if (code && written.contains(a.getOffset()) && run.size() > 0) {
                tables.add(run.toArray(new Address[0]));   // a tabela de antes acaba onde começa esta
                run = new ArrayList<>();
            }
            if (code) {
                run.add(a);
            } else {
                if (run.size() >= 2 || (run.size() >= 1 && written.contains(run.get(0).getOffset()))) {
                    tables.add(run.toArray(new Address[0]));
                }
                run = new ArrayList<>();
            }
            a = a.add(4);
        }
        int created = 0, labelled = 0;
        for (Address[] t : tables) {
            if (!written.contains(t[0].getOffset()) && t.length < 2) continue;
            if (written.contains(t[0].getOffset())) {
                createLabel(t[0], "vtable_" + t[0].toString(), true, SourceType.ANALYSIS);
                labelled++;
            }
            for (Address slot : t) {
                Address to = toAddr(mem.getInt(slot) & 0xffffffffL);
                try {
                    createDWord(slot);
                } catch (Exception e) {
                    // já é um dado
                }
                if (getFunctionAt(to) != null) continue;
                if (getInstructionAt(to) == null) {
                    new DisassembleCommand(new AddressSet(to), null, true).applyTo(currentProgram, monitor);
                }
                if (getInstructionAt(to) == null) continue;
                Function inside = getFunctionContaining(to);
                if (inside != null && !inside.getEntryPoint().equals(to)) {
                    inside.setBody(new AddressSet(inside.getEntryPoint(), to.subtract(1)));
                }
                if (createFunction(to, null) != null) created++;
            }
        }
        analyzeChanges(currentProgram);
        println("XboxVtables: " + tables.size() + " tabelas, " + labelled + " com rótulo, " + created
            + " funções criadas");
    }
}
