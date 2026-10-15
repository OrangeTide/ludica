; viewer256.asm - CGA-256 packed-mode image viewer .COM stub
;
; Loads a cga256pack blob (768-byte RAMDAC palette + 16000 pixel bytes),
; programs the RAMDAC, switches to the 160x100x256 packed mode, and
; copies the image to VRAM.
;
; Build with nasm:
;   nasm -f bin -DIMGFILE='"image.raw"' -o viewer256.com viewer256.asm

%ifndef IMGFILE
%error "define IMGFILE='\"file.raw\"'"
%endif

org 0x100

start:
    ; start from BIOS CGA graphics mode 4 so the CRTC, start address and
    ; VRAM mapping are initialized, then override the mode register.
    mov ah, 0x00
    mov al, 0x04
    int 0x10

    ; load the RAMDAC CLUT: 256 entries x (R,G,B), 6-bit each.
    ; write index 0 to 3C8h, then stream 768 bytes to 3C9h.
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx                  ; dx = 3C9h
    mov si, palette
    mov cx, 256 * 3
    rep outsb

    ; enable packed-256 mode: display enable (0x08) + packed (0x40)
    mov dx, 0x3D8
    mov al, 0x48
    out dx, al

    ; black border/background
    mov dx, 0x3D9
    xor al, al
    out dx, al

    ; copy 16000 pixel bytes to CGA VRAM (B800:0000), linear
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov si, pixels
    mov cx, 160 * 100 / 2
    rep movsw

    ; wait for keypress
    xor ah, ah
    int 0x16

    ; restore text mode (also clears the packed mode bit)
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    ; exit to DOS
    int 0x20

imgdata:
    incbin IMGFILE
palette equ imgdata
pixels  equ imgdata + 768
