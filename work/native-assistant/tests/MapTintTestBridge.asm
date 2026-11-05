EXTERN AAMapTintBridge:PROC
PUBLIC AAMapTintTestReturn
.code
AAMapTintTestCall PROC FRAME
    push rbp
    .pushreg rbp
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov rbp, qword ptr [rsp+50h]
    call AAMapTintBridge
AAMapTintTestReturn::
    add rsp, 20h
    pop rbp
    ret
AAMapTintTestCall ENDP
END
