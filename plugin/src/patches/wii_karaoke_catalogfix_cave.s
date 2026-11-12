# Ported from the standalone wiikaraoke-catalogfix plugin (v8-noop3) - see
# wii_karaoke_catalogfix.cpp for the full writeup. Targeted cave for the
# catalog-update fix: the original code at the patch site unconditionally
# Cancel()s ANY cGameNetCmd*-family object the first time it's swept into
# the manager's tracking list, mid-frame, regardless of the object's own
# download progress - not just our GD0001/GF0001 catalog request. Skipping
# that call unconditionally stalls the Wii U system loading overlay,
# because something else that legitimately needs cancelling right then
# never gets cancelled.
#
# This cave reproduces the two instructions it replaces (reloading the
# command pointer and its vtable), then only skips the Cancel() call when
# the object's own vtable matches cGameNetCmdGetData specifically (set by
# the C side to &DAT_1001cd24 + the title's own .rodata/.data load delta) -
# every other object still gets Cancel()-ed exactly as before.
.globl wkcf_cave
.globl wkcf_vtable_const
.globl wkcf_return_addr

.section .data
.align 4
wkcf_vtable_const: .long 0
wkcf_return_addr:  .long 0

.section .text
.align 4
wkcf_cave:
    lwz   3, 8(1)                  # reload the command pointer (local_30) - original instruction 1
    lwz   11, 0x40(3)               # r11 = its vtable pointer - original instruction 2
    lis   9, wkcf_vtable_const@ha
    lwz   0, wkcf_vtable_const@l(9)
    cmpw  11, 0
    beq   wkcf_skip_cancel
    lwz   12, 0x34(11)              # r12 = vtable[+0x34] (Cancel) - original instruction 3
    mtctr 12                        # original instruction 4
    bctrl                           # original instruction 5: call Cancel() as normal
wkcf_skip_cancel:
    lis   9, wkcf_return_addr@ha
    lwz   0, wkcf_return_addr@l(9)
    mtctr 0
    bctr                            # resume the loop at the untouched continuation code
