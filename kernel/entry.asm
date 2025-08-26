bits 64

section .text.start
global _start
extern kmain

_start:
    mov rbx, rdi

    mov al, 'S'
    call debug_out

    mov al, 'G'
    call debug_out
    lgdt [gdt64_ptr]

    mov al, 'J'
    call debug_out
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov al, 'R'
    call debug_out
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov al, 'K'
    call debug_out
    mov rsp, stack_top

    mov al, 'C'
    call debug_out
    mov rdi, rbx
    call kmain

    cli
.halt:
    hlt
    jmp .halt

debug_out:
    mov dx, 0xE9
    out dx, al
    ret

section .rodata
align 16
gdt64:
    dq 0x0000000000000000
    dq 0x00A09A0000000000
    dq 0x0000920000000000
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64

section .bss
align 16
stack_bottom:
    resb 128 * 1024
stack_top: