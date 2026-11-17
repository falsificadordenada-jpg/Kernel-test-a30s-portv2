#!/usr/bin/env python3
"""A30S eMMC: diagnostics + tolerant EXT_CSD read.   Usage: apply_a30s_emmcdiag.py <xnu-root>
Runs after 30-a30s-emmc.sh (edits bsd/dev/a30s_emmc.c). Idempotent, exact-string edits.

T13 photo: "step ext-csd-done (cmd 8) failed: 60", rintsts=0x24 (CMD_DONE+RXDR, no DATA_OVER),
status=0x00804509 = FIFO FULL with 64 words, AFTER the driver already read 512 bytes.
That means either the FIFO read does not pop (so the 512 bytes read were garbage) or the controller
sends more than BYTCNT. This patch logs enough to tell which one, and keeps going when the data
is plausible even if DATA_OVER never shows up.
"""
import os, sys

REL = "bsd/dev/a30s_emmc.c"


def edit(text, old, new, marker):
    if marker in text:
        return text, False
    if text.count(old) != 1:
        sys.exit("ERROR: expected exactly 1 match, found %d for:\n%s" % (text.count(old), old))
    return text.replace(old, new), True


def main(root):
    path = os.path.join(root, REL)
    t = open(path).read()
    changed = False

    t, c = edit(t,
        "\t\temmc_step = \"ext-csd-fifo\";\n"
        "\t\twhile (done < sizeof(ext_csd)) {\n"
        "\t\t\tuint32_t status = emmc_rd(REG_STATUS);\n"
        "\t\t\tif (!(status & STATUS_FIFO_EMPTY)) {\n"
        "\t\t\t\tuint32_t value = emmc_rd(EMMC_FIFO_OFFSET);\n",
        "\t\temmc_step = \"ext-csd-fifo\";\n"
        "\t\twhile (done < sizeof(ext_csd)) {\n"
        "\t\t\tuint32_t status = emmc_rd(REG_STATUS);\n"
        "\t\t\tif (!(status & STATUS_FIFO_EMPTY)) {\n"
        "\t\t\t\tuint32_t value;\n"
        "\t\t\t\tuint32_t c0 = STATUS_FIFO_COUNT(status); /* A30S_DIAG */\n"
        "\t\t\t\tvalue = emmc_rd(EMMC_FIFO_OFFSET);\n"
        "\t\t\t\tif (done < 16 || done == 256) {\n"
        "\t\t\t\t\tprintf(\"A30S eMMC: diag word@%u=%08x count before=%u after=%u\\n\", done, value,\n"
        "\t\t\t\t\t    c0, STATUS_FIFO_COUNT(emmc_rd(REG_STATUS)));\n"
        "\t\t\t\t}\n",
        "A30S_DIAG")
    changed |= c

    t, c = edit(t,
        "\t\temmc_step = \"ext-csd-done\";\n"
        "\t\twhile (!(emmc_rd(REG_RINTSTS) & INT_DATA_OVER)) {\n"
        "\t\t\tif (emmc_rd(REG_RINTSTS) & INT_ERRORS) {\n"
        "\t\t\t\treturn EIO;\n"
        "\t\t\t}\n"
        "\t\t\tif (mach_absolute_time() >= data_deadline) {\n"
        "\t\t\t\treturn ETIMEDOUT;\n"
        "\t\t\t}\n"
        "\t\t}\n",
        "\t\temmc_step = \"ext-csd-done\";\n"
        "\t\twhile (!(emmc_rd(REG_RINTSTS) & INT_DATA_OVER)) {\n"
        "\t\t\tif (emmc_rd(REG_RINTSTS) & INT_ERRORS) {\n"
        "\t\t\t\treturn EIO;\n"
        "\t\t\t}\n"
        "\t\t\tif (mach_absolute_time() >= data_deadline) { /* A30S_DIAG */\n"
        "\t\t\t\tuint32_t sc;\n"
        "\t\t\t\tmemcpy(&sc, ext_csd + 212, sizeof(sc));\n"
        "\t\t\t\tprintf(\"A30S eMMC: diag no DATA_OVER: bytcnt=%u blksiz=%u tcbcnt=%u tbbcnt=%u fifoth=%08x rintsts=%08x status=%08x\\n\",\n"
        "\t\t\t\t    emmc_rd(REG_BYTCNT), emmc_rd(REG_BLKSIZ), emmc_rd(0x05c), emmc_rd(0x060),\n"
        "\t\t\t\t    emmc_rd(REG_FIFOTH), emmc_rd(REG_RINTSTS), emmc_rd(REG_STATUS));\n"
        "\t\t\t\tprintf(\"A30S eMMC: diag ext_csd[212..215]=%08x [196]=%02x [179]=%02x [183]=%02x\\n\",\n"
        "\t\t\t\t    sc, ext_csd[196], ext_csd[179], ext_csd[183]);\n"
        "\t\t\t\tif (sc != 0 && sc != 0xffffffffu && ext_csd[196] != 0) {\n"
        "\t\t\t\t\tprintf(\"A30S eMMC: EXT_CSD looks sane, continuing without DATA_OVER\\n\");\n"
        "\t\t\t\t\temmc_fifo_reset();\n"
        "\t\t\t\t\tbreak;\n"
        "\t\t\t\t}\n"
        "\t\t\t\treturn ETIMEDOUT;\n"
        "\t\t\t}\n"
        "\t\t}\n",
        "no DATA_OVER: bytcnt")
    changed |= c

    if changed:
        open(path, "w").write(t)
        print("  patched:", REL)
    else:
        print("  already applied:", REL)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
