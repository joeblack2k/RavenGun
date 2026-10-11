# Vertaling (GNU as, Intel-syntaxis) van OpenParrot/src/win64init.asm (MASM), upstream GPL-3.0.
# Main_DoResume springt naar het oorspronkelijke entrypoint met een uitgelijnde stack.
	.intel_syntax noprefix
	.text
	.globl Main_DoResume
Main_DoResume:
	and rsp, -16
	mov rax, -1
	push rax
	mov rax, qword ptr [rip + originalEP]
	jmp rax
