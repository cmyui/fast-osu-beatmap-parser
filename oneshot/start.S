.text
.global _start
.type _start, @function
_start:
    xor %ebp, %ebp
    mov %rsp, %rdi
    and $-16, %rsp
    call fosu_start
    mov %eax, %edi
    mov $60, %eax
    syscall
    ud2
.section .note.GNU-stack,"",@progbits
