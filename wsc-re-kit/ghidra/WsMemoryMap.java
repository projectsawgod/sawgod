// Import a raw ROM into the standard mapper's power-on map, with a ROM0 overlay for every bank.
//@category WonderSwan
import java.io.File;
import java.io.FileInputStream;
import java.io.RandomAccessFile;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.database.mem.FileBytes;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class WsMemoryMap extends WsRom {
    @Override
    public void run() throws Exception {
        File file = new File(getScriptArgs()[0]);
        long size = file.length();
        long[] sizes = {0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x300000,
                        0x400000, 0x600000, 0x800000, 0x1000000};
        if (size < 0x10000 || size % 0x10000 != 0) throw new IllegalArgumentException("Incomplete ROM banks");
        int system, code, save, mapper;
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            in.seek(size - 9); system = in.readUnsignedByte();
            in.seek(size - 6); code = in.readUnsignedByte(); save = in.readUnsignedByte();
            in.seek(size - 3); mapper = in.readUnsignedByte();
        }
        if (code >= sizes.length || sizes[code] != size || mapper > 1)
            throw new IllegalArgumentException("Unsupported footer geometry/mapper");
        currentProgram.getOptions("WSC").setLong("ROM size", size);
        currentProgram.getOptions("WSC").setInt("Mapper", mapper);
        currentProgram.getOptions("WSC").setInt("Save type", save);
        Memory memory = currentProgram.getMemory();
        AddressSpace ram = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (MemoryBlock b : memory.getBlocks()) memory.removeBlock(b, monitor);
        FileBytes bytes;
        try (FileInputStream in = new FileInputStream(file)) {
            bytes = memory.createFileBytes(file.getName(), 0, size, in, monitor);
        }
        MemoryBlock iram = memory.createUninitializedBlock("iram", ram.getAddress(0), (system & 1) != 0 ? 0x10000 : 0x4000, false);
        iram.setPermissions(true, true, true);
        // EEPROM is port driven, so it must not appear as SRAM in the CPU map.
        int[] saves = {0, 0x2000, 0x8000, 0x20000, 0x40000, 0x80000};
        if (save > 0 && save < saves.length) {
            MemoryBlock sram = memory.createUninitializedBlock("sram", ram.getAddress(0x10000), 0x10000, false);
            sram.setPermissions(true, true, false);
            sram.setComment("Cartridge SRAM, C1-selected bank, physical bytes: " + saves[save] + "; smaller SRAM mirrors in this window");
        }
        for (long bank = 2; bank < 16; bank++) {
            long cpu = bank << 16;
            long off = mappedOffset(bank < 4 ? (0xFF0000 | (cpu & 0xFFFF)) : ((0xFFL << 20) | cpu));
            if (off < 0) continue;
            MemoryBlock b = memory.createInitializedBlock(String.format("cpu_%X", bank), ram.getAddress(cpu), bytes, off, 0x10000, false);
            b.setPermissions(true, false, true);
            b.setComment(String.format("Power-on mapping, ROM file offset 0x%X", off));
        }
        for (long bank = 0; bank < size / 0x10000; bank++) {
            MemoryBlock b = memory.createInitializedBlock(String.format("bank_%02X", bank), ram.getAddress(0x20000), bytes, bank << 16, 0x10000, true);
            b.setPermissions(true, false, true);
            b.setComment("This bank through ROM0 (port C2); overlays are separate address spaces");
        }
        createLabel(ram.getAddress(0xFFFF0), "reset_vector", true, SourceType.IMPORTED);
        currentProgram.getSymbolTable().addExternalEntryPoint(ram.getAddress(0xFFFF0));
        disassemble(ram.getAddress(0xFFFF0));
        println("WsMemoryMap: ROM " + size + " bytes, mapper " + mapper + ", save type " + save);
    }
}
