; 512-byte BIOS boot sector. Loads stage2 and the C kernel to 0000:8000.
bits 16
org 0x7c00

%macro LOAD_PART 5
    mov ax, %5
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, %4
    mov ch, %1
    mov cl, %3
    mov dh, %2
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl
    mov ah, 0x02            ; BIOS read sectors 2..18 on floppy head 0
    mov al, 17
    mov ch, 0               ; cylinder 0
    mov cl, 2               ; sector 2 (sector 1 is this boot sector)
    mov dh, 0               ; head 0
    mov dl, [boot_drive]
    mov bx, 0x8000
    int 0x13
    jc disk_error

    ; Load the kernel from subsequent tracks into non-overlapping memory.
    LOAD_PART 0, 1, 1, 18, 0x0a20
    LOAD_PART 1, 0, 1, 18, 0x0c60
    ; Split tracks that would cross a 64 KiB DMA boundary.
    LOAD_PART 1, 1, 1, 11, 0x0ea0
    LOAD_PART 1, 1, 12, 7, 0x1000
    LOAD_PART 2, 0, 1, 18, 0x10e0
    LOAD_PART 2, 1, 1, 18, 0x1320
    LOAD_PART 3, 0, 1, 18, 0x1560
    LOAD_PART 3, 1, 1, 18, 0x17a0
    LOAD_PART 4, 0, 1, 18, 0x19e0
    LOAD_PART 4, 1, 1, 18, 0x1c20
    LOAD_PART 5, 0, 1, 13, 0x1e60
    LOAD_PART 5, 0, 14, 5, 0x2000
    LOAD_PART 5, 1, 1, 18, 0x20a0
    LOAD_PART 6, 0, 1, 18, 0x22e0
    LOAD_PART 6, 1, 1, 18, 0x2520
    LOAD_PART 7, 0, 1, 18, 0x2760

    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_error:
    mov si, error_message
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0e
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive db 0
error_message db 'Disk read error', 0

times 510-($-$$) db 0
dw 0xaa55
