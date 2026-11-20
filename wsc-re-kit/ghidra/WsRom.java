// Shared mapping helpers for the standard WonderSwan cartridge mapper.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;

public abstract class WsRom extends GhidraScript {
    protected long romSize() {
        return currentProgram.getOptions("WSC").getLong("ROM size", 0);
    }

    protected long padding() {
        long size = romSize(), capacity = 0x10000;
        while (capacity < size) capacity <<= 1;
        return capacity - size;
    }

    protected long mappedOffset(long bus) {
        long capacity = romSize() + padding();
        long off = bus % capacity - padding();
        return off >= 0 && off < romSize() ? off : -1;
    }

    protected Address romAddress(long off) {
        if (off < 0 || off >= romSize()) throw new IllegalArgumentException("ROM offset outside image");
        AddressSpace ram = currentProgram.getAddressFactory().getDefaultAddressSpace();
        for (long bank = 15; bank >= 4; bank--) {
            long cpu = (bank << 16) | (off & 0xFFFF);
            if (mappedOffset((0xFFL << 20) | cpu) == off) return ram.getAddress(cpu);
        }
        AddressSpace overlay = currentProgram.getAddressFactory().getAddressSpace(String.format("bank_%02X", off >> 16));
        return overlay.getAddress(0x20000 + (off & 0xFFFF));
    }
}
