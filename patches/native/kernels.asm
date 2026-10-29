; Pure leaf kernels: Win64 volatile registers only, no calls, stack writes or imports.
; They retain ordinary scalar copying outside the measured medium-range cutoff.
_TEXT SEGMENT
PUBLIC KernelMove
KernelMove PROC
    ; rcx=destination, rdx=source, r8d=number of 16-byte TValues
    test r8d, r8d
    jle move_done
    mov r9d, r8d
    shl r9, 4
    cmp rcx, rdx
    jbe move_forward
    lea rax, [rdx+r9]
    cmp rcx, rax
    jae move_forward
    add rcx, r9
    add rdx, r9
    cmp r8d, 32
    jl move_backward_tail
    cmp r8d, 1024
    jg move_backward_tail
move_backward_bulk:
    movups xmm0, XMMWORD PTR [rdx-16]
    movups xmm1, XMMWORD PTR [rdx-32]
    movups xmm2, XMMWORD PTR [rdx-48]
    movups xmm3, XMMWORD PTR [rdx-64]
    movups XMMWORD PTR [rcx-16], xmm0
    movups XMMWORD PTR [rcx-32], xmm1
    movups XMMWORD PTR [rcx-48], xmm2
    movups XMMWORD PTR [rcx-64], xmm3
    sub rcx, 64
    sub rdx, 64
    sub r8d, 4
    cmp r8d, 4
    jge move_backward_bulk
    test r8d, r8d
    jz move_done
move_backward_tail:
    sub rcx, 16
    sub rdx, 16
    movups xmm0, XMMWORD PTR [rdx]
    movups XMMWORD PTR [rcx], xmm0
    dec r8d
    jnz move_backward_tail
    ret
move_forward:
    cmp r8d, 32
    jl move_forward_tail
    cmp r8d, 1024
    jg move_forward_tail
move_forward_bulk:
    movups xmm0, XMMWORD PTR [rdx]
    movups xmm1, XMMWORD PTR [rdx+16]
    movups xmm2, XMMWORD PTR [rdx+32]
    movups xmm3, XMMWORD PTR [rdx+48]
    movups XMMWORD PTR [rcx], xmm0
    movups XMMWORD PTR [rcx+16], xmm1
    movups XMMWORD PTR [rcx+32], xmm2
    movups XMMWORD PTR [rcx+48], xmm3
    add rcx, 64
    add rdx, 64
    sub r8d, 4
    cmp r8d, 4
    jge move_forward_bulk
    test r8d, r8d
    jz move_done
move_forward_tail:
    movups xmm0, XMMWORD PTR [rdx]
    movups XMMWORD PTR [rcx], xmm0
    add rcx, 16
    add rdx, 16
    dec r8d
    jnz move_forward_tail
move_done:
    ret
KernelMove ENDP
PUBLIC KernelByte
KernelByte PROC
    ; L, source, count. Caller has already done luaL_checkstack.
    mov r9, QWORD PTR [rcx+70h]
    test r8d,r8d
    jle byte_done
byte_loop:
    movzx eax,BYTE PTR [rdx]
    cvtsi2sd xmm0,eax
    movsd QWORD PTR [r9],xmm0
    mov DWORD PTR [r9+12],3
    inc rdx
    add r9,16
    dec r8d
    jnz byte_loop
byte_done:
    mov QWORD PTR [rcx+70h],r9
    ret
KernelByte ENDP
PUBLIC KernelSort
KernelSort PROC
    cmp DWORD PTR [rdx+12],3
    jne sort_fallback
    cmp DWORD PTR [r8+12],3
    jne sort_fallback
    xor eax,eax
    movsd xmm0,QWORD PTR [r8]
    comisd xmm0,QWORD PTR [rdx]
    seta al
    ret
sort_fallback:
    ; Patched offline to a direct jump to the exact-build original predicate.
    db 0e9h
    dd 12345678h
KernelSort ENDP
PUBLIC ReadFallback
ReadFallback PROC
    mov QWORD PTR [rsp+8],rbx
    db 0e9h
    dd 12345678h
ReadFallback ENDP
PUBLIC WriteFallback
WriteFallback PROC
    mov QWORD PTR [rsp+8],rbx
    db 0e9h
    dd 12345678h
WriteFallback ENDP
_TEXT ENDS
END
