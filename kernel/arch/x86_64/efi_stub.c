/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* efi_stub.c — x86-64 UEFI entry (the x64 twin of the arm64 efi_stub.c).
 *
 * UEFI hands off in 64-bit long mode with paging on; our kernel is a Multiboot1
 * image that expects 32-bit protected mode at _entry32 (which then rebuilds its
 * own long-mode paging). So this stub: reserves the kernel's load address, does
 * GetMemoryMap + ExitBootServices, copies the embedded kernel image to its
 * absolute link address 0x100000, reads the Multiboot header's entry_addr, and
 * calls the asm trampoline (efi_trampoline.S) that drops long mode -> 32-bit
 * protected mode and jumps to _entry32. kernel_main_x86_64 ignores the Multiboot
 * magic/info, so no boot-info block needs to be synthesized.
 *
 * CRITICAL x64 detail: UEFI uses the Microsoft x64 ABI (args in RCX/RDX/R8/R9).
 * efi_main and every firmware function pointer are marked EFIAPI (ms_abi); a
 * plain SysV call would pass args in the wrong registers. */
typedef unsigned long long u64; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
typedef u64 EFI_STATUS; typedef void* EFI_HANDLE;
typedef struct { u32 a; u16 b; u16 c; u8 d[8]; } EFI_GUID;

#define EFIAPI __attribute__((ms_abi))

/* Serial debug on COM1 (0x3F8) — works before and after ExitBootServices, and is
 * the same console the kernel uses, so the handoff is visible on one wire. */
static inline void outb(u16 p, u8 v){ __asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline u8   inb (u16 p){ u8 v; __asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static void serinit(void){ outb(0x3F8+1,0); outb(0x3F8+3,0x80); outb(0x3F8+0,1); outb(0x3F8+1,0);
                           outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7); outb(0x3F8+4,0x0B); }
static void uputc(char c){ while(!(inb(0x3F8+5)&0x20)){} outb(0x3F8,(u8)c); }
static void uputs(const char*s){ while(*s){ if(*s=='\n') uputc('\r'); uputc(*s++);} }
static void udec(u64 v){ char b[24]; int i=0; if(!v){uputc('0');return;} while(v){b[i++]='0'+v%10;v/=10;} while(i)uputc(b[--i]); }

typedef struct {
  char hdr[24];
  void *RaiseTPL,*RestoreTPL,*AllocatePages,*FreePages,*GetMemoryMap,*AllocatePool,*FreePool;
  void *CreateEvent,*SetTimer,*WaitForEvent,*SignalEvent,*CloseEvent,*CheckEvent;
  void *InstallProtocolInterface,*ReinstallProtocolInterface,*UninstallProtocolInterface;
  void *HandleProtocol,*Reserved,*RegisterProtocolNotify,*LocateHandle,*LocateDevicePath;
  void *InstallConfigurationTable,*LoadImage,*StartImage,*Exit,*UnloadImage,*ExitBootServices;
  void *GetNextMonotonicCount,*Stall,*SetWatchdogTimer,*ConnectController,*DisconnectController;
  void *OpenProtocol,*CloseProtocol,*OpenProtocolInformation,*ProtocolsPerHandle;
  void *LocateHandleBuffer,*LocateProtocol;
} BOOTSVC;
typedef struct {
  char hdr[24]; void*FwVendor; u32 FwRev; void*CInH; void*ConIn; void*COutH; void*ConOut;
  void*CErrH; void*StdErr; void*RuntimeSvc; BOOTSVC *BootSvc;
} SYSTAB;

/* UEFI function-pointer types (all EFIAPI / ms_abi) */
typedef EFI_STATUS (EFIAPI *FN_GMM)(u64*, void*, u64*, u64*, u32*);
typedef EFI_STATUS (EFIAPI *FN_EBS)(EFI_HANDLE, u64);
typedef EFI_STATUS (EFIAPI *FN_POOL)(u32, u64, void**);
typedef EFI_STATUS (EFIAPI *FN_PAGES)(u32, u32, u64, u64*);

/* The asm trampoline: drops to 32-bit protected mode and jumps to `entry`. */
extern void x86_efi_handoff(u64 entry);

/* Embedded flat kernel image (efi_kernel_blob.S). */
extern u8 _kernel_blob_start[]; extern u8 _kernel_blob_end[];

#define KERNEL_LOAD 0x100000ULL   /* Multiboot1 load_addr / link base */

EFIAPI unsigned long efi_main(EFI_HANDLE image, SYSTAB *st){
    serinit();
    uputs("\n>>> ZXV-EFI-STUB [x86_64]: efi_main under UEFI <<<\n");

    FN_PAGES AllocatePages   = (FN_PAGES)st->BootSvc->AllocatePages;
    FN_GMM   GetMemoryMap    = (FN_GMM)  st->BootSvc->GetMemoryMap;
    FN_EBS   ExitBootServices= (FN_EBS)  st->BootSvc->ExitBootServices;
    FN_POOL  AllocatePool    = (FN_POOL) st->BootSvc->AllocatePool;

    u64 ksize = (u64)(_kernel_blob_end - _kernel_blob_start);
    u64 kpages = (ksize + 0xFFF) / 0x1000;

    /* Reserve the kernel's absolute load window [0x100000 .. +image] so nothing
     * firmware-owned sits there when we copy. AllocateAddress (type 2),
     * EfiLoaderData (2). Best-effort: on QEMU this range is conventional. */
    u64 addr = KERNEL_LOAD;
    EFI_STATUS as = AllocatePages(2 /*AllocateAddress*/, 2 /*EfiLoaderData*/, kpages, &addr);
    uputs("reserve 0x100000 status="); udec(as); uputs(" ("); udec(ksize); uputs(" bytes)\n");

    /* ---- GetMemoryMap + ExitBootServices (UEFI spec 7.4) ---- */
    u64 msz=0, mkey=0, dsz=0; u32 dver=0; void *mmap=0;
    GetMemoryMap(&msz,0,&mkey,&dsz,&dver);        /* sizing call (TOO_SMALL) */
    msz += 8*dsz;                                  /* headroom for map growth */
    AllocatePool(2 /*EfiLoaderData*/, msz, &mmap);
    GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);      /* real map + key */
    EFI_STATUS es = ExitBootServices(image, mkey);
    if(es){                                        /* key stale: refresh + retry */
        GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);
        es = ExitBootServices(image, mkey);
    }
    uputs("ExitBootServices status="); udec(es); uputs("\n");

    /* Boot services are gone. Copy the kernel to its link address and hand off. */
    u8 *src = _kernel_blob_start, *dst = (u8*)KERNEL_LOAD;
    for(u64 i=0;i<ksize;i++) dst[i]=src[i];

    /* Multiboot1 header: entry_addr is the 8th dword (file offset 0x1C). */
    u64 entry = (u64)(*(volatile u32*)(KERNEL_LOAD + 0x1C));
    uputs("kernel copied; _entry32="); udec(entry); uputs("\n>>> handing off (long->PM32) <<<\n");

    x86_efi_handoff(entry);       /* never returns */
    for(;;) __asm__ __volatile__("hlt");
    return 0;
}
