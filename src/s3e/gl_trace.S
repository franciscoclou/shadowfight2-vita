/* GL error tracing (gl.c sf_gl_trace): one 8-byte thunk per traced GL
 * import. Thunk i loads i into ip and joins the common path, which calls
 * the real function with the caller's registers and up to 8 stack words,
 * then hands i to sf_gltrace_check, which reads glGetError. The traced
 * entry points take integers and pointers only (float ones go through
 * gl.c's softfp wrappers), so r0-r3 and the stack are all there is.
 */
    .arm
    .text
    .balign 4
    .global sf_gltrace_thunks
sf_gltrace_thunks:
    .set i, 0
    .rept 256
    mov ip, #i
    b sf_gltrace_common
    .set i, i + 1
    .endr

sf_gltrace_common:
    push {r4, r5, ip, lr}
    sub sp, sp, #32
    /* the caller's stack arguments, copied below our frame */
    ldrd r4, r5, [sp, #48]
    strd r4, r5, [sp, #0]
    ldrd r4, r5, [sp, #56]
    strd r4, r5, [sp, #8]
    ldrd r4, r5, [sp, #64]
    strd r4, r5, [sp, #16]
    ldrd r4, r5, [sp, #72]
    strd r4, r5, [sp, #24]
    ldr r4, =sf_gltrace_args
    stm r4, {r0-r3}
    ldr r4, =sf_gltrace_real
    ldr r5, [sp, #40]
    ldr r4, [r4, r5, lsl #2]
    blx r4
    mov r4, r0
    mov r5, r1
    ldr r0, [sp, #40]
    bl sf_gltrace_check
    mov r0, r4
    mov r1, r5
    add sp, sp, #32
    pop {r4, r5, ip, pc}
    .ltorg

    .section .note.GNU-stack, "", %progbits
