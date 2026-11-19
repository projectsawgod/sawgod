// Cooperative context switches for Android's 64-bit ABIs. No third-party context library required.
.text
.global sawgod_fiber_switch
.hidden sawgod_fiber_switch
.type sawgod_fiber_switch, %function
#if defined(__aarch64__)
sawgod_fiber_switch:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    mrs x2, fpcr
    str x2, [x0, #168]
    mrs x2, fpsr
    str x2, [x0, #176]
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x2, [x1, #96]
    mov sp, x2
    ldp d8, d9, [x1, #104]
    ldp d10, d11, [x1, #120]
    ldp d12, d13, [x1, #136]
    ldp d14, d15, [x1, #152]
    ldr x2, [x1, #168]
    msr fpcr, x2
    ldr x2, [x1, #176]
    msr fpsr, x2
    ret
#elif defined(__x86_64__)
sawgod_fiber_switch:
    movq %rsp, 0(%rdi)
    movq %rbx, 8(%rdi)
    movq %rbp, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)
    stmxcsr 56(%rdi)
    fnstcw 60(%rdi)
    movq 0(%rsi), %rsp
    movq 8(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 24(%rsi), %r12
    movq 32(%rsi), %r13
    movq 40(%rsi), %r14
    movq 48(%rsi), %r15
    ldmxcsr 56(%rsi)
    fldcw 60(%rsi)
    ret
#else
#error Android fibers support arm64-v8a and x86_64 only
#endif
.size sawgod_fiber_switch, .-sawgod_fiber_switch

.global sawgod_fiber_start
.hidden sawgod_fiber_start
.type sawgod_fiber_start, %function
sawgod_fiber_start:
#if defined(__aarch64__)
    mov x0, x19
    blr x20
    brk #0
#else
    subq $8, %rsp
    movq %r12, %rdi
    call *%r13
    ud2
#endif
.size sawgod_fiber_start, .-sawgod_fiber_start
.section .note.GNU-stack,"",%progbits
