; Preserve the four floating-point arguments, and pass the actual map caller's
; frame pointer and return address as two extra arguments. No game data writes.
EXTERN AAMapTintPack:PROC
.code
AAMapTintBridge PROC FRAME
    sub rsp, 38h
    .allocstack 38h
    .endprolog
    mov qword ptr [rsp+20h], rbp
    mov rax, qword ptr [rsp+38h]
    mov qword ptr [rsp+28h], rax
    call AAMapTintPack
    add rsp, 38h
    ret
AAMapTintBridge ENDP
END
