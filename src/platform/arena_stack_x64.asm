; Troca de pilha da versão de 64 bits (platform/arena.h, nf::arena::run_on_game_stack): chama fn(arg) com a pilha em
; stack_top (dentro da arena, isto é, dentro da janela dos ponteiros de 32 bits do jogo) e volta para a pilha original.
; O quadro usa rbp como ponteiro de quadro (.setframe), então o desenrolador do Windows sai da pilha nova para a antiga.
;
; void nf_arena_call_on_stack(void (*fn)(void*), void* arg, void* stack_top)

.code
nf_arena_call_on_stack PROC FRAME
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog
    mov rsp, r8
    and rsp, -16
    sub rsp, 32
    mov rax, rcx
    mov rcx, rdx
    call rax
    mov rsp, rbp
    pop rbp
    ret
nf_arena_call_on_stack ENDP
END
