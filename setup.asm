bits 16
org 0x8000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7a00
    mov dword [0x05f0], 0
    ; Query VBE mode 640x480x16 and enable its linear framebuffer.
    mov ax, 0x4f01
    mov cx, 0x0111
    mov di, 0x0600
    int 0x10
    cmp ax, 0x004f
    jne .legacy_video
    mov ax, 0x4f02
    mov bx, 0x4111
    int 0x10
    cmp ax, 0x004f
    jne .legacy_video
    mov dword [0x05f0], 0x31454256 ; "VBE1"
    jmp .video_ready
.legacy_video:
    mov ax, 0x0013
    int 0x10
.video_ready:

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

bits 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9f000
    cld
    call 0x8200             ; C kernel follows this 512-byte setup block
.halt:
    cli
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
