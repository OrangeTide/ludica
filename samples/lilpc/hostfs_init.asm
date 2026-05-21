; hostfs_init.asm - HOSTFS.COM TSR for lilpc
; Made by a machine. PUBLIC DOMAIN (CC0-1.0)
;
; Build: nasm -f bin -o hostfs_init.com hostfs_init.asm
;
; Loaded via INSTALL=A:\HOSTFS.COM in FDCONFIG.SYS.
; Detects the device, gets the SDA from DOS, sets up CDS entries for
; all mounted endpoints, hooks INT 2Fh with the network redirector,
; and goes resident.

[bits 16]
cpu 286
org 0x100

; ---- device I/O ports ----
PORT_BASE   equ 0x0240
REG_CMD     equ PORT_BASE + 0x00   ; W=command, R=status
REG_EP      equ PORT_BASE + 0x01
REG_HND_LO equ PORT_BASE + 0x02
REG_HND_HI equ PORT_BASE + 0x03
REG_PRM_LO equ PORT_BASE + 0x04
REG_PRM_HI equ PORT_BASE + 0x05
REG_POS0   equ PORT_BASE + 0x06
REG_POS1   equ PORT_BASE + 0x07
REG_POS2   equ PORT_BASE + 0x08
REG_POS3   equ PORT_BASE + 0x09
REG_LEN_LO equ PORT_BASE + 0x0A
REG_LEN_HI equ PORT_BASE + 0x0B
REG_XFER   equ PORT_BASE + 0x0C
REG_ID0    equ PORT_BASE + 0x0D
REG_ID1    equ PORT_BASE + 0x0E
REG_ID2    equ PORT_BASE + 0x0F

; ---- device commands ----
CMD_PING       equ 0x00
CMD_MOUNT_INFO equ 0x01
CMD_OPEN       equ 0x02
CMD_CLOSE      equ 0x03
CMD_READ       equ 0x04
CMD_WRITE      equ 0x05
CMD_SEEK       equ 0x06
CMD_STAT       equ 0x07
CMD_CREATE     equ 0x08
CMD_DELETE     equ 0x09
CMD_RENAME     equ 0x0A
CMD_MKDIR      equ 0x0B
CMD_RMDIR      equ 0x0C
CMD_FINDFRST   equ 0x0D
CMD_FINDNEXT   equ 0x0E
CMD_FINDCLOSE  equ 0x0F
CMD_DISKFREE   equ 0x10
CMD_TRUNCATE   equ 0x11
CMD_SETATTR    equ 0x12
CMD_GETSZP     equ 0x13
CMD_INIT       equ 0x14

; ---- SDA offsets (DOS 4.0+ / FreeDOS) ----
SDA_DTA     equ 0x0C       ; DWORD: current DTA / transfer address
SDA_FN1     equ 0x9E       ; 128 bytes: first filename (ASCIIZ)
SDA_FN2     equ 0x11E      ; 128 bytes: second filename (rename)

; ---- SFT entry offsets ----
SFT_REFCNT  equ 0x00       ; WORD: handle count
SFT_MODE    equ 0x02       ; WORD: open mode
SFT_ATTR    equ 0x04       ; BYTE: file attribute
SFT_DEVINFO equ 0x05       ; WORD: device info (bit 15=network)
SFT_CLUST   equ 0x0B       ; WORD: first cluster (we store device handle)
SFT_TIME    equ 0x0D       ; WORD: file time
SFT_DATE    equ 0x0F       ; WORD: file date
SFT_SIZE    equ 0x11       ; DWORD: file size
SFT_POS     equ 0x15       ; DWORD: file position
SFT_NAME    equ 0x20       ; 11 bytes: FCB name (8.3 padded)

; ---- List of Lists offsets ----
LOL_CDS     equ 0x16       ; DWORD: pointer to CDS array
LOL_LASTDRV equ 0x21       ; BYTE: LASTDRIVE value

; ---- CDS entry ----
CDS_SIZE    equ 88
CDS_FLAGS   equ 0x43       ; WORD: flags
CDS_BSOFF   equ 0x4F       ; WORD: backslash offset

; ---- constants ----
FIRST_DRIVE equ 7           ; 0=A .. 7=H
MAX_EP      equ 16
XFER_MAX    equ 4096
CDS_NET_FLAG equ 0x8000

; ======================================================================
; Entry: jump over resident code to transient init
; ======================================================================
    jmp init

; ======================================================================
; INT 2Fh handler - resident portion
; ======================================================================
int2f_handler:
    cmp ah, 0x11
    jne .do_chain
    cld

    cmp al, 0x00
    je fn_install_chk
    cmp al, 0x06
    je fn_close
    cmp al, 0x08
    je fn_read
    cmp al, 0x09
    je fn_write
    cmp al, 0x0C
    je fn_diskspace
    cmp al, 0x0F
    je fn_getattr
    cmp al, 0x11
    je fn_rename
    cmp al, 0x13
    je fn_delete
    cmp al, 0x16
    je fn_open
    cmp al, 0x17
    je fn_create
    cmp al, 0x18
    je fn_create
    cmp al, 0x1B
    je fn_findfirst
    cmp al, 0x1C
    je fn_findnext
    cmp al, 0x21
    je fn_seekend
    cmp al, 0x23
    je fn_qualify
.do_chain:
    jmp far [cs:old_int2f]

; ---- 1100h: installation check ----
fn_install_chk:
    mov al, 0xFF
    iret

; ======================================================================
; Helper: convert drive letter to endpoint number
; Input:  AL = ASCII drive letter
; Output: AL = endpoint (0-15), CF clear if ours; CF set if not
; Clobbers: AH, BX
; ======================================================================
drv_to_ep:
    cmp al, 'a'
    jb .upper
    sub al, 0x20
.upper:
    sub al, 'A'
    jb .bad
    sub al, FIRST_DRIVE
    jb .bad
    cmp al, MAX_EP
    jae .bad
    push cx
    mov cl, al
    mov bx, 1
    shl bx, cl
    test [cs:mount_mask], bx
    pop cx
    jz .bad
    clc
    ret
.bad:
    stc
    ret

; ======================================================================
; Helper: write AX to device handle register
; ======================================================================
h_write_handle:
    push dx
    mov dx, REG_HND_LO
    out dx, al
    xchg al, ah
    mov dx, REG_HND_HI
    out dx, al
    xchg al, ah
    pop dx
    ret

; ======================================================================
; Helper: read device handle register into AX
; ======================================================================
h_read_handle:
    push dx
    mov dx, REG_HND_LO
    in al, dx
    mov ah, al
    mov dx, REG_HND_HI
    in al, dx
    xchg al, ah
    pop dx
    ret

; ======================================================================
; Helper: write DX:CX to POS registers
; ======================================================================
h_write_pos:
    push ax
    push bx
    mov bx, dx
    mov al, cl
    mov dx, REG_POS0
    out dx, al
    mov al, ch
    inc dx
    out dx, al
    mov al, bl
    inc dx
    out dx, al
    mov al, bh
    inc dx
    out dx, al
    pop bx
    pop ax
    ret

; ======================================================================
; Helper: read POS registers into DX:AX
; ======================================================================
h_read_pos:
    push bx
    push cx
    mov dx, REG_POS0
    in al, dx
    mov bl, al
    inc dx
    in al, dx
    mov bh, al
    inc dx
    in al, dx
    mov cl, al
    inc dx
    in al, dx
    mov ch, al
    mov ax, bx
    mov dx, cx
    pop cx
    pop bx
    ret

; ======================================================================
; Helper: write CX to LEN registers
; ======================================================================
h_write_len:
    push ax
    push dx
    mov al, cl
    mov dx, REG_LEN_LO
    out dx, al
    mov al, ch
    inc dx
    out dx, al
    pop dx
    pop ax
    ret

; ======================================================================
; Helper: read LEN registers into AX
; ======================================================================
h_read_len:
    push dx
    mov dx, REG_LEN_LO
    in al, dx
    mov ah, al
    inc dx
    in al, dx
    xchg al, ah
    pop dx
    ret

; ======================================================================
; Helper: issue command AL, return status in AL with ZF set if OK
; ======================================================================
h_do_cmd:
    push dx
    mov dx, REG_CMD
    out dx, al
    mov dx, REG_CMD
    in al, dx
    test al, al
    pop dx
    ret

; ======================================================================
; Helper: write NUL-terminated string at DS:SI to XFER port
; ======================================================================
h_xfer_write_str:
    push ax
    push dx
    mov dx, REG_XFER
.loop:
    lodsb
    out dx, al
    test al, al
    jnz .loop
    pop dx
    pop ax
    ret

; ======================================================================
; Common return sequences
; ======================================================================
ret_ok:
    pop es
    pop ds
    popa
    clc
    retf 2

ret_err:
    pop es
    pop ds
    popa
    mov al, [cs:tmp_err]
    xor ah, ah
    stc
    retf 2

ret_chain:
    pop es
    pop ds
    popa
    jmp far [cs:old_int2f]

; ======================================================================
; 1123h: qualify remote filename
; DS:SI = source, ES:DI = 128-byte output buffer
; ======================================================================
fn_qualify:
    push ax
    lodsb
    call drv_to_ep
    jc .not_ours
    dec si

    ; check if the filename after "X:" or "X:\" is a DOS device name
    push si
    push di
    mov di, si
    add di, 2              ; skip drive letter and colon
    cmp byte [di], '\'
    jne .chk_dev
    inc di                 ; skip leading backslash
.chk_dev:
    call is_device_name
    pop di
    pop si
    jc .not_ours           ; it's a device name; let DOS handle it

    ; copy drive letter (uppercased)
    lodsb
    cmp al, 'a'
    jb .q1
    cmp al, 'z'
    ja .q1
    sub al, 0x20
.q1:
    stosb
    ; copy colon
    lodsb
    stosb
    ; ensure backslash after colon
    cmp byte [si], '\'
    je .copy
    mov al, '\'
    stosb
.copy:
    lodsb
    cmp al, 'a'
    jb .store
    cmp al, 'z'
    ja .store
    sub al, 0x20
.store:
    stosb
    test al, al
    jnz .copy
    pop ax
    clc
    retf 2
.not_ours:
    dec si
    pop ax
    jmp far [cs:old_int2f]

; ======================================================================
; Helper: find basename in DS:DI path (advance DI past last backslash)
; Input:  DS:DI = ASCIIZ path (e.g. "SUBDIR\CON")
; Output: DS:DI = pointer to basename (e.g. "CON")
; ======================================================================
basename_ptr:
    push ax
    push si
    mov si, di
.bp_scan:
    lodsb
    test al, al
    jz .bp_done
    cmp al, '\'
    jne .bp_scan
    mov di, si             ; DI = char after backslash
    jmp .bp_scan
.bp_done:
    pop si
    pop ax
    ret

; ======================================================================
; Helper: check if DS:DI points to a DOS device name (NUL-terminated)
; Returns CF set if it IS a device name (should not be redirected)
; Clobbers: nothing (saves/restores all used regs)
; ======================================================================
is_device_name:
    push ax
    push bx
    push cx
    push si
    push di

    ; uppercase-copy up to 8 chars from DS:DI into a temp area
    mov si, di
    sub sp, 10
    mov di, sp
    push ss
    pop es
    mov cx, 8
.idn_copy:
    lodsb
    test al, al
    jz .idn_pad
    cmp al, '.'           ; stop at extension
    je .idn_pad
    cmp al, 'a'
    jb .idn_store
    cmp al, 'z'
    ja .idn_store
    sub al, 0x20
.idn_store:
    stosb
    loop .idn_copy
.idn_pad:
    mov al, 0
    stosb

    ; compare against known device names
    mov si, sp
    push cs
    pop es

    mov di, dev_names
.idn_next:
    cmp byte [es:di], 0
    je .idn_no_match
    push si
.idn_cmp:
    mov al, [es:di]
    test al, al
    jz .idn_check_end
    mov ah, [ss:si]
    cmp al, ah
    jne .idn_skip
    inc si
    inc di
    jmp .idn_cmp
.idn_check_end:
    cmp byte [ss:si], 0
    je .idn_match
    ; check for trailing digit (COM1-4, LPT1-3)
    cmp byte [ss:si+1], 0
    jne .idn_skip2
    mov al, [ss:si]
    cmp al, '1'
    jb .idn_skip2
    cmp al, '9'
    jbe .idn_match
.idn_skip2:
    pop si
    ; advance DI past the rest of this name
.idn_adv:
    cmp byte [es:di], 0
    je .idn_adv_done
    inc di
    jmp .idn_adv
.idn_adv_done:
    inc di                 ; skip the NUL terminator
    jmp .idn_next
.idn_skip:
    pop si
    jmp .idn_adv
.idn_match:
    pop si                 ; balance push si
    add sp, 10             ; free temp buffer
    ; restore ES to what caller expects (DS segment)
    push ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret
.idn_no_match:
    add sp, 10             ; free temp buffer
    push ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

dev_names:
    db 'CON', 0
    db 'PRN', 0
    db 'AUX', 0
    db 'NUL', 0
    db 'COM', 0            ; matches COM + digit (COM1-COM9)
    db 'LPT', 0            ; matches LPT + digit (LPT1-LPT9)
    db 0                   ; end of list

; ======================================================================
; 1106h: open existing file
; ======================================================================
fn_open:
    pusha
    push ds
    push es

    mov [cs:tmp_sft_off], di
    mov [cs:tmp_sft_seg], es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    ; check for device names (CON, PRN, etc.) in the path
    push ax
    lea di, [si + SDA_FN1 + 3]
    call basename_ptr
    call is_device_name
    pop ax
    jc ret_chain

    push ax
    mov dx, REG_EP
    out dx, al

    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    mov al, CMD_OPEN
    call h_do_cmd
    jnz .open_err

    call h_read_handle
    mov bx, ax

    call h_write_handle
    mov al, CMD_GETSZP
    call h_do_cmd
    call h_read_pos             ; DX:AX = size

    les di, [cs:tmp_sft_off]
    mov word [es:di + SFT_REFCNT], 1
    mov [es:di + SFT_CLUST], bx
    mov [es:di + SFT_SIZE], ax
    mov [es:di + SFT_SIZE + 2], dx
    mov word [es:di + SFT_POS], 0
    mov word [es:di + SFT_POS + 2], 0

    pop ax
    add al, FIRST_DRIVE
    mov ah, 0x80
    mov [es:di + SFT_DEVINFO], ax

    jmp ret_ok

.open_err:
    pop bx
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1106h: close file
; ======================================================================
fn_close:
    pusha
    push ds
    push es

    mov ax, [es:di + SFT_CLUST]
    call h_write_handle
    mov al, CMD_CLOSE
    call h_do_cmd

    jmp ret_ok

; ======================================================================
; 1108h: read from file
; ======================================================================
fn_read:
    pusha
    push ds
    push es

    mov [cs:tmp_sft_off], di
    mov [cs:tmp_sft_seg], es

    mov ax, [es:di + SFT_CLUST]
    call h_write_handle

    mov cx, [es:di + SFT_POS]
    mov dx, [es:di + SFT_POS + 2]
    call h_write_pos

    push bp
    mov bp, sp
    mov cx, [bp + 18]
    pop bp

    cmp cx, XFER_MAX
    jbe .rd_len_ok
    mov cx, XFER_MAX
.rd_len_ok:
    call h_write_len

    mov al, CMD_READ
    call h_do_cmd
    jnz .rd_err

    call h_read_len
    mov cx, ax

    push cx
    lds si, [cs:sda_ptr]
    les di, [si + SDA_DTA]
    mov dx, REG_XFER
    rep insb
    pop cx

    les di, [cs:tmp_sft_off]
    add [es:di + SFT_POS], cx
    adc word [es:di + SFT_POS + 2], 0

    mov [cs:tmp_cx], cx
    pop es
    pop ds
    popa
    mov cx, [cs:tmp_cx]
    clc
    retf 2

.rd_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1109h: write to file
; ======================================================================
fn_write:
    pusha
    push ds
    push es

    mov [cs:tmp_sft_off], di
    mov [cs:tmp_sft_seg], es

    mov ax, [es:di + SFT_CLUST]
    call h_write_handle

    mov cx, [es:di + SFT_POS]
    mov dx, [es:di + SFT_POS + 2]
    call h_write_pos

    push bp
    mov bp, sp
    mov cx, [bp + 18]
    pop bp

    cmp cx, XFER_MAX
    jbe .wr_len_ok
    mov cx, XFER_MAX
.wr_len_ok:
    call h_write_len

    push cx
    lds si, [cs:sda_ptr]
    lds si, [si + SDA_DTA]
    mov dx, REG_XFER
    rep outsb
    pop cx

    mov al, CMD_WRITE
    call h_do_cmd
    jnz .wr_err

    call h_read_len
    mov cx, ax

    les di, [cs:tmp_sft_off]
    add [es:di + SFT_POS], cx
    adc word [es:di + SFT_POS + 2], 0

    mov ax, [es:di + SFT_POS + 2]
    cmp ax, [es:di + SFT_SIZE + 2]
    ja .wr_grow
    jb .wr_no_grow
    mov ax, [es:di + SFT_POS]
    cmp ax, [es:di + SFT_SIZE]
    jbe .wr_no_grow
.wr_grow:
    mov ax, [es:di + SFT_POS]
    mov [es:di + SFT_SIZE], ax
    mov ax, [es:di + SFT_POS + 2]
    mov [es:di + SFT_SIZE + 2], ax
.wr_no_grow:

    mov [cs:tmp_cx], cx
    pop es
    pop ds
    popa
    mov cx, [cs:tmp_cx]
    clc
    retf 2

.wr_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1116h/1117h/1118h: create file
; ======================================================================
fn_create:
    pusha
    push ds
    push es

    mov [cs:tmp_sft_off], di
    mov [cs:tmp_sft_seg], es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    ; check for device names
    push ax
    lea di, [si + SDA_FN1 + 3]
    call basename_ptr
    call is_device_name
    pop ax
    jc ret_chain

    push ax
    mov dx, REG_EP
    out dx, al

    xor al, al
    mov dx, REG_PRM_LO
    out dx, al
    mov dx, REG_PRM_HI
    out dx, al

    lds si, [cs:sda_ptr]
    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    mov al, CMD_CREATE
    call h_do_cmd
    jnz .cr_err

    call h_read_handle
    mov bx, ax

    les di, [cs:tmp_sft_off]
    mov word [es:di + SFT_REFCNT], 1
    mov [es:di + SFT_CLUST], bx
    mov word [es:di + SFT_SIZE], 0
    mov word [es:di + SFT_SIZE + 2], 0
    mov word [es:di + SFT_POS], 0
    mov word [es:di + SFT_POS + 2], 0

    pop ax
    add al, FIRST_DRIVE
    mov ah, 0x80
    mov [es:di + SFT_DEVINFO], ax

    jmp ret_ok

.cr_err:
    pop bx
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1113h: delete file
; ======================================================================
fn_delete:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    ; check for device names
    push ax
    lea di, [si + SDA_FN1 + 3]
    call basename_ptr
    call is_device_name
    pop ax
    jc ret_chain

    mov dx, REG_EP
    out dx, al

    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    mov al, CMD_DELETE
    call h_do_cmd
    jnz .del_err
    jmp ret_ok

.del_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1111h: rename file
; ======================================================================
fn_rename:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    mov dx, REG_EP
    out dx, al

    push si
    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    pop si
    lea si, [si + SDA_FN2 + 3]
    call h_xfer_write_str

    mov al, CMD_RENAME
    call h_do_cmd
    jnz .ren_err
    jmp ret_ok

.ren_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 110Fh: get file attributes
; ======================================================================
fn_getattr:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    ; check for device names
    push ax
    lea di, [si + SDA_FN1 + 3]
    call basename_ptr
    call is_device_name
    pop ax
    jc ret_chain

    mov dx, REG_EP
    out dx, al

    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    mov al, CMD_STAT
    call h_do_cmd
    jnz .ga_err

    mov dx, REG_PRM_LO
    in al, dx

    xor ah, ah
    mov [cs:tmp_ax], ax
    pop es
    pop ds
    popa
    mov ax, [cs:tmp_ax]
    clc
    retf 2

.ga_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 110Ch: get disk space
; ======================================================================
fn_diskspace:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    mov dx, REG_EP
    out dx, al

    mov al, CMD_DISKFREE
    call h_do_cmd
    jnz .ds_err

    mov dx, REG_PRM_LO
    in al, dx
    xor ah, ah
    mov [cs:tmp_ax], ax

    mov dx, REG_PRM_HI
    in al, dx
    xor ah, ah
    shl ax, 4
    mov [cs:tmp_cx], ax

    mov word [cs:tmp_bx], 0x7FFF
    mov word [cs:tmp_dx], 0xFFFF

    pop es
    pop ds
    popa
    mov ax, [cs:tmp_ax]
    mov bx, [cs:tmp_bx]
    mov cx, [cs:tmp_cx]
    mov dx, [cs:tmp_dx]
    clc
    retf 2

.ds_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 111Bh: find first
; ======================================================================
fn_findfirst:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    mov al, [si + SDA_FN1]
    call drv_to_ep
    jc ret_chain

    ; save drive ID byte for DTA[0]: 0x80 | (1-based drive number)
    push ax
    add al, FIRST_DRIVE + 1
    or al, 0x80
    mov [cs:tmp_drv], al
    pop ax

    mov dx, REG_EP
    out dx, al

    mov al, 0x37
    mov dx, REG_PRM_LO
    out dx, al
    xor al, al
    mov dx, REG_PRM_HI
    out dx, al

    lea si, [si + SDA_FN1 + 3]
    call h_xfer_write_str

    mov al, CMD_FINDFRST
    call h_do_cmd
    jnz .ff_err

    call h_read_handle
    push ax

    lds si, [cs:sda_ptr]
    les di, [si + SDA_DTA]

    ; DTA[0] = drive ID byte (0x80 | 1-based drive number)
    ; DOS reads this in FindNext to route to the correct handler
    mov al, [cs:tmp_drv]
    mov [es:di], al

    pop ax
    mov [es:di + 1], ax         ; DTA[1:2] = search handle

    add di, 21                  ; DI -> SearchDir (DTA+21)
    mov dx, REG_XFER
    mov cx, 32                  ; 32-byte FAT directory entry
    rep insb

    jmp ret_ok

.ff_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 111Ch: find next
; ======================================================================
fn_findnext:
    pusha
    push ds
    push es

    lds si, [cs:sda_ptr]
    les di, [si + SDA_DTA]

    mov ax, [es:di + 1]            ; search handle at DTA[1:2]
    call h_write_handle

    mov al, CMD_FINDNEXT
    call h_do_cmd
    jnz .fn_err

    lds si, [cs:sda_ptr]
    les di, [si + SDA_DTA]
    add di, 21                  ; DI -> SearchDir (DTA+21)
    mov dx, REG_XFER
    mov cx, 32                  ; 32-byte FAT directory entry
    rep insb

    jmp ret_ok

.fn_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; 1121h: seek from end
; ======================================================================
fn_seekend:
    pusha
    push ds
    push es

    mov ax, [es:di + SFT_CLUST]
    call h_write_handle
    mov al, CMD_GETSZP
    call h_do_cmd
    jnz .sk_err

    call h_read_pos
    mov cx, ax
    call h_write_pos

    mov al, CMD_SEEK
    call h_do_cmd
    jnz .sk_err

    call h_read_pos
    mov [es:di + SFT_POS], ax
    mov [es:di + SFT_POS + 2], dx

    jmp ret_ok

.sk_err:
    mov [cs:tmp_err], al
    jmp ret_err

; ======================================================================
; Resident data area
; ======================================================================
old_int2f:   dd 0
sda_ptr:     dd 0
mount_mask:  dw 0
tmp_sft_off: dw 0
tmp_sft_seg: dw 0
tmp_cx:      dw 0
tmp_ax:      dw 0
tmp_bx:      dw 0
tmp_dx:      dw 0
tmp_err:     db 0
tmp_drv:     db 0

resident_end:

; ======================================================================
; Transient init - everything below is released after going resident
; ======================================================================
init:
    ; detect device
    mov dx, REG_ID0
    in al, dx
    cmp al, 'H'
    jne .no_dev
    mov dx, REG_ID1
    in al, dx
    cmp al, 'F'
    jne .no_dev
    mov dx, REG_ID2
    in al, dx
    cmp al, 'S'
    jne .no_dev

    ; get SDA via INT 21h AX=5D06h
    mov ax, 0x5D06
    int 0x21
    ; DS:SI = SDA
    mov [cs:sda_ptr], si
    mov [cs:sda_ptr+2], ds

    ; store SDA in device via CMD_INIT (POS = seg:off)
    mov ax, si
    mov dx, REG_POS0
    out dx, al
    mov al, ah
    mov dx, REG_POS1
    out dx, al
    mov ax, ds
    mov dx, REG_POS2
    out dx, al
    mov al, ah
    mov dx, REG_POS3
    out dx, al
    mov al, CMD_INIT
    mov dx, REG_CMD
    out dx, al
    in al, dx
    test al, al
    jnz .init_err

    ; restore DS
    push cs
    pop ds

    ; get List of Lists
    mov ah, 0x52
    int 0x21
    ; ES:BX = List of Lists

    mov cl, [es:bx + LOL_LASTDRV]
    mov [lastdrive], cl
    mov ax, [es:bx + LOL_CDS]
    mov [cds_off], ax
    mov ax, [es:bx + LOL_CDS + 2]
    mov [cds_seg], ax

    ; probe endpoints and set up CDS
    xor si, si
    xor bp, bp

.ep_loop:
    cmp si, MAX_EP
    jge .ep_done

    mov ax, si
    mov dx, REG_EP
    out dx, al

    mov al, CMD_MOUNT_INFO
    mov dx, REG_CMD
    out dx, al
    in al, dx
    test al, al
    jnz .ep_next

    mov dx, REG_LEN_LO
    in al, dx
    test al, al
    jz .ep_next

    ; build mount_mask
    push cx
    mov cx, si
    mov bx, 1
    shl bx, cl
    or [mount_mask], bx
    pop cx

    ; compute drive number
    mov ax, si
    add ax, FIRST_DRIVE
    cmp al, [lastdrive]
    jge .ep_next

    mov bl, al
    add bl, 'A'

    mov cx, CDS_SIZE
    mul cx
    les di, [cds_off]
    add di, ax

    mov [es:di], bl
    mov byte [es:di + 1], ':'
    mov byte [es:di + 2], '\'
    mov byte [es:di + 3], 0

    mov word [es:di + CDS_FLAGS], CDS_NET_FLAG
    mov word [es:di + CDS_BSOFF], 2

    inc bp

.ep_next:
    inc si
    jmp .ep_loop

.ep_done:
    test bp, bp
    jz .no_mounts

    ; hook INT 2Fh
    push cs
    pop ds
    mov ax, 0x352F              ; get INT 2Fh vector
    int 0x21
    mov [old_int2f], bx
    mov [old_int2f+2], es
    mov dx, int2f_handler
    mov ax, 0x252F              ; set INT 2Fh vector
    int 0x21

    ; print banner
    mov ax, bp
    call print_dec
    mov dx, msg_ok
    mov ah, 0x09
    int 0x21

    ; go resident: keep PSP + resident code
    mov dx, (resident_end - $$  + 0x100 + 0x0F) >> 4
    mov ax, 0x3100              ; terminate and stay resident, exit code 0
    int 0x21

.no_mounts:
    push cs
    pop ds
    mov dx, msg_nomount
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C00
    int 0x21

.no_dev:
    push cs
    pop ds
    mov dx, msg_nodev
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21

.init_err:
    push cs
    pop ds
    mov dx, msg_err
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21

; ---- print AL as 1-2 digit decimal ----
print_dec:
    push ax
    push bx
    push dx
    xor ah, ah
    mov bl, 10
    div bl
    mov dl, ah
    test al, al
    jz .ones
    push dx
    add al, '0'
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop dx
.ones:
    mov al, dl
    add al, '0'
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop dx
    pop bx
    pop ax
    ret

; ---- transient data ----
cds_off:    dw 0
cds_seg:    dw 0
lastdrive:  db 0

msg_ok:     db ' drive(s) mounted', 13, 10, '$'
msg_nodev:  db 'HOSTFS: device not found', 13, 10, '$'
msg_err:    db 'HOSTFS: init failed', 13, 10, '$'
msg_nomount: db 'HOSTFS: no drives mounted', 13, 10, '$'
