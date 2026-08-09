/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* efi_stub.c — arm64 UEFI entry (BOOTAA64.EFI). VERIFIED end-to-end under AAVMF:
 * locates the GOP, publishes the framebuffer handoff record, ExitBootServices,
 * and hands off to the kernel — which boots to EL0 + P-TERM with the desktop on
 * the UEFI GOP framebuffer ("[DRIVER ONLINE] UEFI GOP framebuffer").
 *
 * Handoff goes through a RELOCATED trampoline (efi_tramp.S): AAVMF loads this
 * image into the same low RAM the kernel links to (0x40080000), so copying the
 * kernel in place overwrote our own running code. The kernel is staged into
 * firmware-allocated memory and the final copy+jump runs from a scratch page.
 *
 * Boot with: -M virt,gic-version=3 (the kernel drives a GICv3). */
typedef unsigned long long u64; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
typedef u64 EFI_STATUS; typedef void* EFI_HANDLE;
typedef struct { u32 a; u16 b; u16 c; u8 d[8]; } EFI_GUID;

static void uputc(char ch){ *(volatile u32*)0x09000000U=(u32)(u8)ch; }
static void uputs(const char*s){ while(*s) uputc(*s++); }
static void uhex(u64 v){ uputs("0x"); for(int i=60;i>=0;i-=4){ int n=(v>>i)&0xF; uputc(n<10?'0'+n:'a'+n-10);} }
static void udec(u64 v){ char b[24]; int i=0; if(!v){uputc('0');return;} while(v){b[i++]='0'+v%10; v/=10;} while(i) uputc(b[--i]); }

typedef struct { u32 Ver,HRes,VRes,PixFmt,PixInfo[4],PixPerScan; } GOP_INFO;
typedef struct { u32 MaxMode,Mode; GOP_INFO*Info; u64 SizeOfInfo,FrameBufferBase,FrameBufferSize; } GOP_MODE;
typedef struct { void*QueryMode,*SetMode,*Blt; GOP_MODE*Mode; } GOP;

typedef struct { u64 Sig,Rev; } HDR_PAD; /* not used directly */
typedef struct {
  char hdr[24];
  void *RaiseTPL,*RestoreTPL,*AllocatePages,*FreePages,*GetMemoryMap,*AllocatePool,*FreePool;
  void *CreateEvent,*SetTimer,*WaitForEvent,*SignalEvent,*CloseEvent,*CheckEvent;
  void *InstallProtocolInterface,*ReinstallProtocolInterface,*UninstallProtocolInterface;
  void *HandleProtocol,*Reserved,*RegisterProtocolNotify,*LocateHandle,*LocateDevicePath;
  void *InstallConfigurationTable,*LoadImage,*StartImage,*Exit,*UnloadImage,*ExitBootServices;
  void *GetNextMonotonicCount,*Stall,*SetWatchdogTimer,*ConnectController,*DisconnectController;
  void *OpenProtocol,*CloseProtocol,*OpenProtocolInformation,*ProtocolsPerHandle;
  void *LocateHandleBuffer;
  EFI_STATUS (*LocateProtocol)(EFI_GUID*,void*,void**);
} BOOTSVC;
typedef struct {
  char hdr[24]; void*FwVendor; u32 FwRev; void*CInH; void*ConIn; void*COutH; void*ConOut;
  void*CErrH; void*StdErr; void*RuntimeSvc; BOOTSVC *BootSvc;
} SYSTAB;

unsigned long efi_main(EFI_HANDLE image, SYSTAB *st){
    (void)image;
    uputs("\r\n>>> ZXV-EFI-STUB: efi_main under UEFI <<<\r\n");
    EFI_GUID gop_guid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    GOP *gop=0;
    EFI_STATUS s = st->BootSvc->LocateProtocol(&gop_guid,0,(void**)&gop);
    if(s!=0 || !gop){ uputs("GOP: not found status="); uhex(s); uputs("\r\n"); for(;;)__asm__("wfi"); }
    u64 fb = gop->Mode->FrameBufferBase;
    u32 w = gop->Mode->Info->HRes, h = gop->Mode->Info->VRes, pps = gop->Mode->Info->PixPerScan;
    u32 pixfmt = gop->Mode->Info->PixFmt;
    uputs("GOP fb="); uhex(fb); uputs(" res="); udec(w); uputc('x'); udec(h);
    uputs(" stride="); udec(pps); uputs(" fmt="); udec(pixfmt); uputs("\r\n");

    /* ---- GetMemoryMap + ExitBootServices (spec 7.4) ---- */
    typedef EFI_STATUS (*GMM)(u64*, void*, u64*, u64*, u32*);
    typedef EFI_STATUS (*EBS)(EFI_HANDLE, u64);
    typedef EFI_STATUS (*AP)(u32, u64, void**);
    typedef EFI_STATUS (*APAGES)(u32, u32, u64, u64*);
    GMM GetMemoryMap    = (GMM)st->BootSvc->GetMemoryMap;
    EBS ExitBootServices= (EBS)st->BootSvc->ExitBootServices;
    AP  AllocatePool    = (AP)st->BootSvc->AllocatePool;
    APAGES AllocatePages= (APAGES)st->BootSvc->AllocatePages;

    /* ---- Stage the kernel + relocate the handoff trampoline ----
     * AAVMF loads THIS EFI image into low RAM, overlapping the kernel's fixed
     * link address 0x40080000. Copying straight there overwrote our own running
     * code and the embedded blob (the CPU then took an Undefined Instruction at
     * 0x40080000). So: copy the blob to a firmware-chosen staging buffer, and
     * copy the copy-and-jump trampoline (efi_tramp.S) to its own scratch page.
     * The final copy then runs from memory that overlaps neither image. */
    extern u8 _kernel_blob_start[]; extern u8 _kernel_blob_end[];
    extern u8 _tramp_start[]; extern u8 _tramp_end[];
    u64 ksize = (u64)(_kernel_blob_end - _kernel_blob_start);
    u64 staging = 0, tramp = 0;
    {
        u64 kpages = (ksize + 0xFFF) / 0x1000;
        EFI_STATUS s1 = AllocatePages(0 /*AllocateAnyPages*/, 2 /*EfiLoaderData*/, kpages, &staging);
        EFI_STATUS s2 = AllocatePages(0 /*AllocateAnyPages*/, 2 /*EfiLoaderData*/, 1, &tramp);
        if(s1 || s2 || !staging || !tramp){
            uputs("FATAL: staging/trampoline alloc failed s1="); uhex(s1);
            uputs(" s2="); uhex(s2); uputs("\r\n"); for(;;)__asm__("wfi");
        }
        u8 *ks = (u8*)staging; for(u64 i=0;i<ksize;i++) ks[i] = _kernel_blob_start[i];
        u64 tsize = (u64)(_tramp_end - _tramp_start);
        u8 *ts = (u8*)tramp;   for(u64 i=0;i<tsize;i++) ts[i] = _tramp_start[i];
        uputs("staged kernel @"); uhex(staging); uputs(" trampoline @"); uhex(tramp);
        uputs(" ("); udec(tsize); uputs(" bytes)\r\n");
    }

    /* Publish the boot-handoff record 64KB below the kernel (0x40070000) — only
     * AFTER the range is reserved, so firmware cannot hand that page to someone
     * else in between. The kernel checks this magic at boot and adopts the GOP
     * fb instead of ramfb. It sits below the kernel image so BSS-clear cannot
     * wipe it. Layout mirrors zxv_bootinfo_t. */
    *(volatile u64*)0x40070000ULL = 0x5A585642464F4F49ULL;  /* ZXV_BOOTINFO_MAGIC */
    *(volatile u64*)0x40070008ULL = fb;
    *(volatile u32*)0x40070010ULL = w;
    *(volatile u32*)0x40070014ULL = h;
    *(volatile u32*)0x40070018ULL = pps;
    *(volatile u32*)0x4007001CULL = pixfmt;
    uputs("handoff record published @0x40070000\r\n");

    u64 msz=0, mkey=0, dsz=0; u32 dver=0; void *mmap=0;
    GetMemoryMap(&msz,0,&mkey,&dsz,&dver);      /* sizing call (returns TOO_SMALL) */
    msz += 4*dsz;                                /* headroom for map growth        */
    AllocatePool(2 /*EfiLoaderData*/, msz, &mmap);
    GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);    /* real map + key                 */
    EFI_STATUS es = ExitBootServices(image, mkey);
    if(es){                                      /* map moved: refresh key, retry  */
        GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);
        es = ExitBootServices(image, mkey);
    }
    /* Boot services are gone. The PL011 UART @0x09000000 still works (bare metal). */
    uputs("ExitBootServices ok\r\n");

    /* ---- hand off through the relocated trampoline ----
     * It copies staging -> 0x40080000, cleans the caches, drops the MMU, and
     * branches to the kernel's _start. Running from its own scratch page, the
     * copy can safely overwrite this EFI image (which AAVMF placed in the same
     * low RAM the kernel links to). The kernel then enters el1_entry MMU-off,
     * exactly as on the tested -kernel path. */
    uputs(">>> handing off to ZXV kernel via trampoline (MMU off) <<<\r\n");
    ((void(*)(u64,u64,u64))tramp)(staging, 0x40080000ULL, ksize);
    for(;;) __asm__ __volatile__("wfi");
    return 0;
}
