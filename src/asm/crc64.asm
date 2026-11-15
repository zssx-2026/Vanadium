; Vanadium kernel - CRC-64/XZ (reflected, poly 0xC96C5795D7870F42) slicing-by-8.
; Windows x64 ABI: rcx = data, rdx = len, r8 = seed (running crc), returns rax.
; The 8x256 table is built once on first use into .bss, so no C++ table is exported.
; Keep in sync with crc64() in src/core/crypto.cpp (C fallback + self-test vector).

BITS 64
DEFAULT REL

%define POLY 0xC96C5795D7870F42

section .bss
align 64
crc_table: resq 8*256
table_ready: resd 1

section .text
global vd_crc64_asm

; ---------------------------------------------------------------- build_table
; Fills crc_table[0][i] then derives slices 1..7. Clobbers rax,rcx,rdx,rsi,rdi,r9.
build_table:
    lea     rdi, [crc_table]
    xor     ecx, ecx                ; i = 0
.gen0:
    mov     rax, rcx
    mov     edx, 8
.bit:
    mov     rsi, rax
    shr     rax, 1
    and     rsi, 1
    neg     rsi
    mov     r9, POLY
    and     r9, rsi
    xor     rax, r9
    dec     edx
    jnz     .bit
    mov     [rdi + rcx*8], rax
    inc     ecx
    cmp     ecx, 256
    jb      .gen0

    mov     ecx, 1                  ; slice k = 1..7
.slice:
    mov     eax, ecx
    shl     eax, 8                  ; offset of slice k
    lea     rsi, [rdi + rax*8]
    mov     edx, ecx
    dec     edx
    shl     edx, 8                  ; offset of slice k-1
    lea     rdx, [rdi + rdx*8]
    xor     r8d, r8d                ; i = 0
.derive:
    mov     rax, [rdx + r8*8]
    mov     r9, rax
    shr     rax, 8
    and     r9d, 0xFF
    xor     rax, [rdi + r9*8]
    mov     [rsi + r8*8], rax
    inc     r8d
    cmp     r8d, 256
    jb      .derive
    inc     ecx
    cmp     ecx, 8
    jb      .slice

    mov     dword [table_ready], 1
    ret

; ------------------------------------------------------------------ vd_crc64
; u64 vd_crc64_asm(const void* data, size_t len, u64 seed)
vd_crc64_asm:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12, rcx                ; data
    mov     r13, rdx                ; len
    mov     r14, r8                 ; crc
    mov     r15, r8
    not     r14                     ; crc = ~seed (XZ init/xorout are all ones)

    cmp     dword [table_ready], 0
    jne     .have_table
    call    build_table
.have_table:
    lea     rbx, [crc_table]

    cmp     r13, 8
    jb      .tail

    mov     r8, r13
    shr     r8, 3                   ; number of 8-byte blocks
.block:
    mov     rax, [r12]              ; little-endian load
    xor     rax, r14                ; crc ^= block
    ; crc = T7[b0] ^ T6[b1] ^ T5[b2] ^ T4[b3] ^ T3[b4] ^ T2[b5] ^ T1[b6] ^ T0[b7]
    movzx   ecx, al
    mov     r14, [rbx + 7*2048 + rcx*8]
    movzx   ecx, ah
    xor     r14, [rbx + 6*2048 + rcx*8]
    mov     rcx, rax
    shr     rcx, 16
    movzx   edx, cl
    xor     r14, [rbx + 5*2048 + rdx*8]
    movzx   edx, ch
    xor     r14, [rbx + 4*2048 + rdx*8]
    mov     rcx, rax
    shr     rcx, 32
    movzx   edx, cl
    xor     r14, [rbx + 3*2048 + rdx*8]
    movzx   edx, ch
    xor     r14, [rbx + 2*2048 + rdx*8]
    mov     rcx, rax
    shr     rcx, 48
    movzx   edx, cl
    xor     r14, [rbx + 1*2048 + rdx*8]
    movzx   edx, ch
    xor     r14, [rbx + 0*2048 + rdx*8]

    add     r12, 8
    add     r13, -8
    dec     r8
    jnz     .block

.tail:
    test    r13, r13
    jz      .done
.tail_loop:
    movzx   eax, byte [r12]
    mov     rcx, r14
    xor     cl, al
    movzx   ecx, cl
    shr     r14, 8
    xor     r14, [rbx + rcx*8]
    inc     r12
    dec     r13
    jnz     .tail_loop

.done:
    mov     rax, r14
    not     rax
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret
