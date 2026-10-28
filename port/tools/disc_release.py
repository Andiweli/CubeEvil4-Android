"""Which release of Resident Evil 4 (GameCube) a disc header belongs to.

The same rule as runtime/dvd.cpp (re4_disc_release); it reads only the disc header
(sys/boot.bin, the first 0x440 bytes of an image):

    debug       G4BE08 with the file system table at 0x28FE00: the debug build of
                2004-11-25 (its main.dol is 2554432 bytes), the build the port's code is
    retail-eu   G4BP08: the European release (port/docs/retail-eu-plan.md)
    retail-us   G4BE08 with the table elsewhere: the US release (port/docs/retail-us-plan.md)
    unknown     anything else
"""
import struct

DEBUG_FST_OFFSET = 0x28FE00
RELEASES = ("debug", "retail-eu", "retail-us", "unknown")
SUPPORTED = ("debug", "retail-eu", "retail-us")


def release(boot: bytes) -> str:
    if len(boot) < 0x42C:
        return "unknown"
    game_id = boot[:6]
    fst_offset = struct.unpack_from(">I", boot, 0x424)[0]
    if game_id == b"G4BE08":
        return "debug" if fst_offset == DEBUG_FST_OFFSET else "retail-us"
    if game_id == b"G4BP08":
        return "retail-eu"
    return "unknown"
