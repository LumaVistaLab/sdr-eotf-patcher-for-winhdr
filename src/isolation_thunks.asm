option casemap:none
EXTERN DisplayRenderHook:PROC
EXTERN LegacyRenderHook:PROC
EXTERN LinkedShaderHook:PROC
EXTERN CommonShaderHook:PROC
.code
; Preserve all volatile argument registers, including XMM, to respect DWM's IPRA calls.
; All three profiled entries have at most four integer/pointer arguments and return HRESULT.
WRAP MACRO entry:req, handler:req, fifth:=<0>
PUBLIC entry
entry PROC FRAME
    sub rsp, 0d8h
    .allocstack 0d8h
    .endprolog
    mov [rsp+30h], rcx
    mov [rsp+38h], rdx
    mov [rsp+40h], r8
    mov [rsp+48h], r9
    mov [rsp+50h], r10
    mov [rsp+58h], r11
    movdqu [rsp+60h], xmm0
    movdqu [rsp+70h], xmm1
    movdqu [rsp+80h], xmm2
    movdqu [rsp+90h], xmm3
    movdqu [rsp+0a0h], xmm4
    movdqu [rsp+0b0h], xmm5
    IF fifth
        mov rax, [rsp+100h]
        mov [rsp+20h], rax
    ENDIF
    call handler
    mov rcx, [rsp+30h]
    mov rdx, [rsp+38h]
    mov r8, [rsp+40h]
    mov r9, [rsp+48h]
    mov r10, [rsp+50h]
    mov r11, [rsp+58h]
    movdqu xmm0, [rsp+60h]
    movdqu xmm1, [rsp+70h]
    movdqu xmm2, [rsp+80h]
    movdqu xmm3, [rsp+90h]
    movdqu xmm4, [rsp+0a0h]
    movdqu xmm5, [rsp+0b0h]
    add rsp, 0d8h
    ret
entry ENDP
ENDM
WRAP DisplayRenderThunk, DisplayRenderHook
WRAP LegacyRenderThunk, LegacyRenderHook
WRAP LinkedShaderThunk, LinkedShaderHook
WRAP CommonShaderThunk, CommonShaderHook, 1
END
