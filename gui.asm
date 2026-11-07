bits 16
org 0x8000

%macro RECT 5
    mov word [rect_x], %1
    mov word [rect_y], %2
    mov word [rect_w], %3
    mov word [rect_h], %4
    mov byte [rect_color], %5
    call draw_rect
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7a00
    sti

    mov ax, 0x0013          ; VGA 320x200, 256 colors
    int 0x10
    call draw_desktop

.input:
    xor ah, ah
    int 0x16                ; wait for key
    cmp al, 27              ; Esc exits to BIOS
    je .exit
    cmp al, 'q'
    je .exit
    cmp al, 'w'
    je .toggle_window
    cmp ah, 0x4b            ; left arrow
    je .left
    cmp ah, 0x4d            ; right arrow
    je .right
    cmp ah, 0x48            ; up arrow
    je .up
    cmp ah, 0x50            ; down arrow
    je .down
    jmp .input

.left:
    cmp word [cursor_x], 2
    jbe .redraw
    sub word [cursor_x], 3
    jmp .redraw
.right:
    cmp word [cursor_x], 310
    jae .redraw
    add word [cursor_x], 3
    jmp .redraw
.up:
    cmp word [cursor_y], 12
    jbe .redraw
    sub word [cursor_y], 3
    jmp .redraw
.down:
    cmp word [cursor_y], 178
    jae .redraw
    add word [cursor_y], 3
    jmp .redraw
.toggle_window:
    xor byte [window_open], 1
.redraw:
    call draw_desktop
    jmp .input
.exit:
    mov ax, 0x0003          ; return to text mode
    int 0x10
    int 0x19                ; reboot through BIOS

; Draw the whole screen so the keyboard-controlled cursor stays clean.
draw_desktop:
    pusha
    mov ax, 0xa000
    mov es, ax
    RECT 0, 0, 320, 200, 9       ; blue background
    RECT 0, 0, 320, 14, 1        ; title bar
    RECT 0, 184, 320, 16, 8      ; taskbar
    RECT 4, 187, 42, 11, 7       ; start button

    cmp byte [window_open], 0
    je .no_window
    RECT 56, 38, 212, 112, 0     ; shadow
    RECT 52, 34, 212, 112, 7     ; window body
    RECT 52, 34, 212, 14, 1      ; window title
    RECT 59, 56, 198, 80, 15     ; content area
.no_window:
    ; Tiny keyboard-controlled cursor.
    mov ax, [cursor_x]
    mov [rect_x], ax
    mov ax, [cursor_y]
    mov [rect_y], ax
    mov word [rect_w], 5
    mov word [rect_h], 5
    mov byte [rect_color], 14
    call draw_rect
    popa

    mov byte [text_color], 15
    mov word [text_x], 8
    mov word [text_y], 3
    mov si, title_text
    call print_at
    mov word [text_x], 8
    mov word [text_y], 21
    mov si, hint_text
    call print_at
    mov word [text_x], 8
    mov word [text_y], 188
    mov si, start_text
    call print_at
    cmp byte [window_open], 0
    je .done
    mov word [text_x], 64
    mov word [text_y], 37
    mov si, window_title
    call print_at
    mov word [text_x], 64
    mov word [text_y], 66
    mov byte [text_color], 0
    mov si, window_line1
    call print_at
    mov word [text_x], 64
    mov word [text_y], 80
    mov si, window_line2
    call print_at
.done:
    ret

; Draw rectangle from rect_x/y/w/h/color. ES must point at A000h.
draw_rect:
    pusha
    mov ax, [rect_y]
    mov bx, 320
    mul bx
    add ax, [rect_x]
    mov di, ax
    mov al, [rect_color]
    mov bx, [rect_h]
.row:
    mov cx, [rect_w]
    rep stosb
    add di, 320
    sub di, [rect_w]
    dec bx
    jnz .row
    popa
    ret

; Draw 5x7 bitmap text directly in the 320x200 framebuffer.
print_at:
    pusha
.next:
    lodsb
    test al, al
    jz .end
    cmp al, 'a'
    jb .uppercase
    cmp al, 'z'
    ja .uppercase
    sub al, 32
.uppercase:
    cmp al, ' '
    jne .lookup
    add word [text_x], 6
    jmp .next
.lookup:
    mov di, font_chars
    xor cx, cx
.find:
    cmp al, [di]
    je .found
    inc di
    inc cx
    cmp byte [di], 0
    jne .find
    add word [text_x], 6
    jmp .next
.found:
    ; glyph address = font_data + glyph_index * 7
    mov ax, cx
    mov bx, 7
    mul bx
    add ax, font_data
    mov [glyph_base], ax
    mov [text_ptr], si
    mov byte [glyph_row], 0
.glyph_row:
    mov si, [glyph_base]
    xor ax, ax
    mov al, [glyph_row]
    add si, ax
    mov bl, [si]
    mov ax, [text_y]
    xor cx, cx
    mov cl, [glyph_row]
    add ax, cx
    mov cx, 320
    mul cx
    add ax, [text_x]
    mov di, ax
    mov cx, 5
.glyph_pixel:
    test bl, 0x10
    jz .skip_pixel
    mov al, [text_color]
    mov byte [es:di], al
.skip_pixel:
    inc di
    shl bl, 1
    loop .glyph_pixel
    inc byte [glyph_row]
    cmp byte [glyph_row], 7
    jb .glyph_row
    add word [text_x], 6
    mov si, [text_ptr]
    jmp .next
.end:
    popa
    ret

cursor_x dw 150
cursor_y dw 100
window_open db 1
rect_x dw 0
rect_y dw 0
rect_w dw 0
rect_h dw 0
rect_color db 0
text_x dw 0
text_y dw 0
text_ptr dw 0
glyph_base dw 0
glyph_row db 0
text_color db 15
title_text db 'NASM OS - QEMU i386', 0
hint_text db 'ARROWS MOVE | W WINDOW | Q QUIT', 0
start_text db 'MENU', 0
window_title db 'WELCOME', 0
window_line1 db 'TINY OS RUNNING', 0
window_line2 db 'NASM / QEMU i386', 0

; Seven 5-bit rows per glyph, in font_chars order.
font_chars db 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:|./', 0
font_data:
    db 14,17,17,31,17,17,17 ; A
    db 30,17,17,30,17,17,30 ; B
    db 14,17,16,16,16,17,14 ; C
    db 30,17,17,17,17,17,30 ; D
    db 31,16,16,30,16,16,31 ; E
    db 31,16,16,30,16,16,16 ; F
    db 14,17,16,23,17,17,15 ; G
    db 17,17,17,31,17,17,17 ; H
    db 14,4,4,4,4,4,14 ; I
    db 7,2,2,2,18,18,12 ; J
    db 17,18,20,24,20,18,17 ; K
    db 16,16,16,16,16,16,31 ; L
    db 17,27,21,21,17,17,17 ; M
    db 17,25,21,19,17,17,17 ; N
    db 14,17,17,17,17,17,14 ; O
    db 30,17,17,30,16,16,16 ; P
    db 14,17,17,17,21,18,13 ; Q
    db 30,17,17,30,20,18,17 ; R
    db 15,16,16,14,1,1,30 ; S
    db 31,4,4,4,4,4,4 ; T
    db 17,17,17,17,17,17,14 ; U
    db 17,17,17,17,17,10,4 ; V
    db 17,17,17,21,21,21,10 ; W
    db 17,17,10,4,10,17,17 ; X
    db 17,17,10,4,4,4,4 ; Y
    db 31,1,2,4,8,16,31 ; Z
    db 14,17,19,21,25,17,14 ; 0
    db 4,12,4,4,4,4,14 ; 1
    db 14,17,1,2,4,8,31 ; 2
    db 30,1,1,14,1,1,30 ; 3
    db 2,6,10,18,31,2,2 ; 4
    db 31,16,16,30,1,1,30 ; 5
    db 14,16,16,30,17,17,14 ; 6
    db 31,1,2,4,8,8,8 ; 7
    db 14,17,17,14,17,17,14 ; 8
    db 14,17,17,15,1,1,14 ; 9
    db 0,0,0,31,0,0,0 ; -
    db 0,4,4,0,4,4,0 ; :
    db 4,4,4,4,4,4,4 ; |
    db 0,1,2,4,8,16,0 ; .
    db 1,2,4,8,16,0,0 ; /
