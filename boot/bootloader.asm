bits 16
org 0x7C00

start:
    ; Инициализация сегментов
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Сообщение о загрузке
    mov si, msg_loading
    call print_string

    ; Загрузка ядра с диска
    mov ah, 0x02
    mov al, 32       ; Количество секторов для чтения
    mov ch, 0        ; Цилиндр
    mov cl, 2        ; Сектор (начинается с 2)
    mov dh, 0        ; Головка
    mov dl, 0x80     ; Диск (0x80 для первого жесткого диска)
    mov bx, 0x1000   ; Сегмент
    mov es, bx
    mov bx, 0x0000   ; Смещение
    int 0x13
    jc disk_error

    ; Переход в защищенный режим
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:protected_mode

disk_error:
    mov si, msg_disk_error
    call print_string
    jmp $

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

bits 32
protected_mode:
    ; Настройка сегментов
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000

    ; Копируем ядро в правильное место
    mov esi, 0x10000
    mov edi, 0x200000
    mov ecx, 0x10000 / 4
    rep movsd

    ; Подготовка к переходу в длинный режим
    call enable_paging
    jmp 0x08:long_mode

enable_paging:
    ; Настройка страничной организации памяти
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, 4096
    rep stosd
    mov edi, cr3

    ; PML4
    mov dword [edi], 0x2003
    add edi, 0x1000
    ; PDP
    mov dword [edi], 0x3003
    add edi, 0x1000
    ; PD
    mov dword [edi], 0x4003
    add edi, 0x1000

    ; Заполняем таблицу страниц
    mov ebx, 0x00000003
    mov ecx, 512
.set_entry:
    mov dword [edi], ebx
    add ebx, 0x1000
    add edi, 8
    loop .set_entry

    ; Включаем PAE и длинный режим
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

bits 64
long_mode:
    ; Настройка сегментов для длинного режима
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Переход к ядру
    mov rax, 0x200000
    xor rbx, rbx  ; boot_info = NULL для BIOS
    jmp rax

; Данные
msg_loading db "Loading AlwexOS...", 13, 10, 0
msg_disk_error db "Disk error!", 13, 10, 0

; GDT
gdt_start:
    dq 0x0000000000000000    ; Null descriptor
    dq 0x00AF9A000000FFFF    ; Code descriptor
    dq 0x00AF92000000FFFF    ; Data descriptor
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510-($-$$) db 0
dw 0xAA55
