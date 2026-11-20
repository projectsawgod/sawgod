// Apply normalized text CDL opcode starts. Run tools/cdl.py before this script.
//@category WonderSwan
import java.nio.file.Files;
import java.nio.file.Path;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.AddressSet;

public class ApplyCdl extends WsRom {
    @Override
    public void run() throws Exception {
        AddressSet seeds = new AddressSet();
        for (String line : Files.readAllLines(Path.of(getScriptArgs()[0]))) {
            String text = line.split("#", 2)[0].trim();
            if (text.isEmpty()) continue;
            String[] f = text.split("\\s+");
            if ((Integer.parseInt(f[1]) & 1) != 0) seeds.add(romAddress(Long.parseLong(f[0], 16)));
        }
        new DisassembleCommand(seeds, null, false).applyTo(currentProgram, monitor);
        println("ApplyCdl: seeded " + seeds.getNumAddresses() + " opcode addresses");
    }
}
