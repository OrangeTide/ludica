; viewer.asm - CGA image viewer .COM stub
; Made by a machine. PUBLIC DOMAIN (CC0-1.0)
;
; Build with nasm:
;   nasm -f bin -DMODE=4 -DIMGFILE='"image.raw"' -o viewer.com viewer.asm
;   nasm -f bin -DMODE=6 -DIMGFILE='"image.raw"' -o viewer.com viewer.asm

%ifndef MODE
%error "define MODE=4 or MODE=6"
%endif
%ifndef IMGFILE
%error "define IMGFILE='\"file.raw\"'"
%endif

org 0x100

start:
    ; set video mode
    mov ah, 0x00
    mov al, MODE
    int 0x10

%if MODE == 4
    ; select palette 1 high intensity
    mov ah, 0x0B
    mov bh, 0x01
    mov bl, 0x01
    int 0x10
    ; set background/border to black
    mov ah, 0x0B
    mov bh, 0x00
    mov bl, 0x00
    int 0x10
%elif MODE == 6
    ; enable colorburst for composite artifact colors
    ; BIOS sets mode_ctrl to 0x1E (bit 2 = BW/no burst); clear bit 2
    mov dx, 0x3D8
    mov al, 0x1A
    out dx, al
    ; keep foreground white, clear upper bits (BIOS sets 0x3F)
    inc dx
    mov al, 0x0F
    out dx, al
%endif

    ; copy image data to CGA VRAM (B800:0000)
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov si, imgdata
    mov cx, 16384 / 2
    rep movsw

    ; wait for keypress
    xor ah, ah
    int 0x16

    ; restore text mode
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; exit to DOS
    int 0x20

imgdata:
    incbin IMGFILE
