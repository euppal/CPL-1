bits 32

global i686_Ring0Executor_Invoke
global i686_Ring0Executor_InvokeISRHandler

section .text

i686_Ring0Executor_Invoke:
    mov edx, esp
    mov ecx, dword [edx + 4]
    mov edx, dword [edx + 8]
    int 0xff
    ret

i686_Ring0Executor_InvokeISRHandler:
    pusha

    push es
    push fs
    push gs
    push ds

    mov eax, 0x10
    mov es, eax
    mov fs, eax
    mov gs, eax
    mov ds, eax

    push edx
    call ecx
    pop edx

    pop ds
    pop gs
    pop fs
    pop es
    popa

    iretd