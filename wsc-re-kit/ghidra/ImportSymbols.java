// Rows: rom|func|ram HEX_OFFSET NAME [DECIMAL_SIZE]. func explicitly requests a ROM function.
//@category WonderSwan
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ImportSymbols extends WsRom {
    @Override
    public void run() throws Exception {
        int count = 0;
        Set<String> names = new HashSet<>(), addresses = new HashSet<>();
        for (String line : Files.readAllLines(Path.of(getScriptArgs()[0]))) {
            String[] parts = line.split("#", 2);
            String text = parts[0].trim();
            if (text.isEmpty()) continue;
            String[] f = text.split("\\s+");
            if (f.length < 3 || f.length > 4 || !(f[0].equals("rom") || f[0].equals("func") || f[0].equals("ram")) ||
                !f[2].matches("[A-Za-z_][A-Za-z0-9_]*")) throw new IllegalArgumentException("Invalid symbol row: " + text);
            long off = Long.parseLong(f[1], 16);
            long size = f.length == 4 ? Long.parseLong(f[3]) : 0;
            if (!names.add(f[2]) || !addresses.add((f[0].equals("ram") ? "ram" : "rom") + ":" + off)) throw new IllegalArgumentException("Duplicate symbol");
            if (size < 0 || off < 0 || (!f[0].equals("ram") && off + Math.max(1, size) > romSize()) ||
                (f[0].equals("ram") && off + Math.max(1, size) > 0x20000)) throw new IllegalArgumentException("Symbol outside memory");
            Address address = !f[0].equals("ram") ? romAddress(off) :
                currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off);
            createLabel(address, f[2], true, SourceType.IMPORTED);
            if (f[0].equals("func")) {
                new DisassembleCommand(address, null, true).applyTo(currentProgram, monitor);
                if (getFunctionAt(address) == null) new CreateFunctionCmd(address).applyTo(currentProgram, monitor);
                Function function = getFunctionAt(address);
                if (function != null) function.setName(f[2], SourceType.IMPORTED);
            }
            String comment = parts.length == 2 ? parts[1].trim() : "";
            if (size > 0) comment += (comment.isEmpty() ? "" : "\n") + "Symbol size: " + size + " bytes";
            if (!comment.isEmpty()) setPlateComment(address, comment);
            count++;
        }
        println("ImportSymbols: " + count + " labels");
    }
}
