// Assertions for the generated synthetic cartridges; run by test_ghidra.py.
//@category WonderSwan
import ghidra.app.script.GhidraScript;

public class ValidateMap extends GhidraScript {
    private void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    @Override
    public void run() throws Exception {
        long size = Long.parseLong(getScriptArgs()[0]);
        boolean sram = getScriptArgs()[1].equals("sram");
        require(currentProgram.getOptions("WSC").getLong("ROM size", 0) == size, "ROM size metadata");
        require((currentProgram.getMemory().getBlock("sram") != null) == sram, "save hardware map");
        require(currentProgram.getMemory().getBlock("iram").getSize() == 0x10000, "WSC RAM size");
        for (long b = 0; b < size / 0x10000; b++) {
            require(currentProgram.getMemory().getBlock(String.format("bank_%02X", b)) != null, "bank overlay missing");
        }
        require((getByte(toAddr(0xFFFF0)) & 0xFF) == 0xEA, "reset bytes");
        require(getInstructionAt(toAddr(0xFC000)) != null, "CDL code start");
        require(getInstructionAt(toAddr(0xFC020)) != null, "near call code");
        require(getInstructionAt(toAddr(0xFC030)) != null, "far call code");
        require(getFunctionAt(toAddr(0xFC020)) != null, "explicit function symbol");
        require(currentProgram.getSymbolTable().getGlobalSymbols("startup").size() == 1, "ROM symbol");
        require(currentProgram.getSymbolTable().getGlobalSymbols("frame_counter").size() == 1, "RAM symbol");
        println("ValidateMap: mapping, code, and symbols passed");
    }
}
