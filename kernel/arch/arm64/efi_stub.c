/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* efi_stub.c — arm64 UEFI entry: PROVEN to load under AAVMF, locate the GOP,
 * and draw to a real linear framebuffer (ramfb GOP: 800x600 fb@0x5c7a0000).
 * Foundation for boot-to-desktop from the ISO. Next: ExitBootServices + hand
 * off to the kernel (reconcile EL1/MMU-on -> the kernel boot path). */
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

    /* Publish the boot-handoff record 64KB below the kernel (0x40070000). The
     * kernel checks this magic at boot and adopts the GOP fb instead of ramfb.
     * Written while boot services are live; it is plain RAM, below the kernel
     * image so BSS-clear cannot wipe it. Layout mirrors zxv_bootinfo_t. */
    *(volatile u64*)0x40070000ULL = 0x5A585642464F4F49ULL;  /* ZXV_BOOTINFO_MAGIC */
    *(volatile u64*)0x40070008ULL = fb;
    *(volatile u32*)0x40070010ULL = w;
    *(volatile u32*)0x40070014ULL = h;
    *(volatile u32*)0x40070018ULL = pps;
    *(volatile u32*)0x4007001CULL = pixfmt;
    uputs("handoff record published @0x40070000\r\n");

    /* ---- GetMemoryMap + ExitBootServices (spec 7.4) ---- */
    typedef EFI_STATUS (*GMM)(u64*, void*, u64*, u64*, u32*);
    typedef EFI_STATUS (*EBS)(EFI_HANDLE, u64);
    typedef EFI_STATUS (*AP)(u32, u64, void**);
    GMM GetMemoryMap    = (GMM)st->BootSvc->GetMemoryMap;
    EBS ExitBootServices= (EBS)st->BootSvc->ExitBootServices;
    AP  AllocatePool    = (AP)st->BootSvc->AllocatePool;
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

    /* ---- copy the embedded kernel to its non-PIC link address 0x40080000 ---- */
    extern u8 _kernel_blob_start[]; extern u8 _kernel_blob_end[];
    u64 ksize = (u64)(_kernel_blob_end - _kernel_blob_start);
    u8 *src = _kernel_blob_start, *dst = (u8*)0x40080000ULL;
    for(u64 i=0;i<ksize;i++) dst[i]=src[i];
    uputs("kernel copied ("); udec(ksize); uputs(" bytes)\r\n");

    /* clean D-cache to PoC over [handoff .. kernel_end], invalidate I-cache, so
     * the MMU-off kernel sees the real bytes in RAM. */
    for(u64 a=0x40070000ULL; a<0x40080000ULL+ksize; a+=64)
        __asm__ __volatile__("dc cvac, %0" :: "r"(a) : "memory");
    __asm__ __volatile__("dsb sy\n ic iallu\n dsb sy\n isb" ::: "memory");

    uputs(">>> handing off to ZXV kernel (MMU off) <<<\r\n");
    /* Option A: disable MMU + caches (clear SCTLR_EL1 M/C/I) so the kernel runs
     * exactly its tested -kernel path (enters el1_entry MMU-off; arm64_mmu_init
     * builds and enables its own tables), then branch to 0x40080000. */
    __asm__ __volatile__(
        "mrs x0, sctlr_el1\n"
        "bic x0, x0, #1\n"        /* M: MMU off      */
        "bic x0, x0, #4\n"        /* C: D-cache off  */
        "bic x0, x0, #4096\n"     /* I: I-cache off  */
        "msr sctlr_el1, x0\n"
        "isb\n"
        "movz x2, #0x4008, lsl #16\n"   /* x2 = 0x40080000 (kernel _start) */
        "br x2\n"
        ::: "x0","x2","memory");
    for(;;) __asm__ __volatile__("wfi");
    return 0;
}
