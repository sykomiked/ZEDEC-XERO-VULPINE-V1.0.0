/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
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

/* __kernel_end of kernel_arm64.elf (BSS and stack included), from the
 * Makefile. The flat image ends where the BSS begins, so its size alone
 * would understate the window by about 230 MB. */
#ifndef ZXV_KERNEL_END
#    error "build with -DZXV_KERNEL_END=<__kernel_end of kernel_arm64.elf>"
#endif
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

#define EFIAPI             /* AArch64 UEFI calls use the plain AAPCS64 ABI */
#include "../efi_models.h" /* golden disc: verify \ZXV\MODELS\*.gguf */

unsigned long efi_main(EFI_HANDLE image, SYSTAB *st)
{
    uputs("\r\n>>> ZXV-EFI-STUB: efi_main under UEFI <<<\r\n");
    EFI_GUID gop_guid = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    GOP *gop=0;
    /* GOP is OPTIONAL. When no display device is present LocateProtocol returns
     * EFI_NOT_FOUND (status ...0e); we must DEGRADE to a headless serial boot,
     * never stall. have_gop gates every subsequent GOP read + the fb handoff. */
    u64 fb=0; u32 w=0,h=0,pps=0,pixfmt=0;
    int have_gop=1;
    EFI_STATUS s = st->BootSvc->LocateProtocol(&gop_guid,0,(void**)&gop);
    if(s!=0 || !gop){
        uputs("GOP: not present (status="); uhex(s);
        uputs(") -- booting headless/serial, no framebuffer handoff\r\n");
        have_gop=0;
    }
    if(have_gop){
        fb = gop->Mode->FrameBufferBase;
        w = gop->Mode->Info->HRes; h = gop->Mode->Info->VRes; pps = gop->Mode->Info->PixPerScan;
        pixfmt = gop->Mode->Info->PixFmt;
        uputs("GOP fb="); uhex(fb); uputs(" res="); udec(w); uputc('x'); udec(h);
        uputs(" stride="); udec(pps); uputs(" fmt="); udec(pixfmt); uputs("\r\n");
    }

    /* The kernel's whole window, image + BSS + stack: 0x40070000 (the
     * handoff pages) to __kernel_end, which the Makefile passes in. First the
     * RAM must cover it, or the kernel cannot run on this machine: say so and
     * stop, rather than hand off into memory that is not there (a silent hang
     * otherwise). Then reserve as much of it as the firmware gives, in 2 MiB
     * pieces, so the boot model and its scratch are placed outside it. Pieces
     * the firmware already uses (this loader's own image among them) are left;
     * the kernel still checks the model's regions itself. */
    {
        typedef EFI_STATUS (*GMM0)(u64 *, void *, u64 *, u64 *, u32 *);
        typedef EFI_STATUS (*AP0)(u32, u64, void **);
        typedef EFI_STATUS (*FP0)(void *);
        typedef EFI_STATUS (*APG0)(u32, u32, u64, u64 *);
        u64 lo = 0x40000000ULL, hi = (ZXV_KERNEL_END + 0xFFFULL) & ~0xFFFULL;
        u64 msz = 0, mkey = 0, dsz = 0;
        u32 dver = 0;
        u8 *map = 0;
        ((GMM0) st->BootSvc->GetMemoryMap)(&msz, 0, &mkey, &dsz, &dver);
        msz += 8 * dsz;
        int covered = 0;
        if (dsz && ((AP0) st->BootSvc->AllocatePool)(2, msz, (void **) &map) == 0 &&
            ((GMM0) st->BootSvc->GetMemoryMap)(&msz, map, &mkey, &dsz, &dver) == 0) {
            /* Walk up from lo: each step jumps to the end of the RAM
             * descriptor holding a, so it ends within one pass of the map. */
            u64 a = lo, n = msz / dsz;
            for (u64 step = 0; step <= n && a < hi; step++) {
                u64 next = a;
                for (u64 i = 0; i < n; i++) {
                    const u8 *d = map + i * dsz;
                    u32 type = *(const u32 *) d;
                    u64 ps = *(const u64 *) (d + 8), np = *(const u64 *) (d + 24);
                    int ram = (type >= 1 && type <= 7) || type == 9 || type == 10;
                    if (ram && ps <= a && a < ps + np * 0x1000ULL) next = ps + np * 0x1000ULL;
                }
                if (next == a) break;
                a = next;
            }
            covered = a >= hi;
            ((FP0) st->BootSvc->FreePool)(map);
        }
        if (!covered) {
            uputs("[BOOT_FAIL] this kernel needs RAM from 0x40000000 to ");
            uhex(hi);
            uputs(" (");
            udec((hi - lo) >> 20);
            uputs(" MiB for its image, BSS and stack); "
                  "this machine has less there. Give it more memory.\r\n");
            for (;;) __asm__ __volatile__("wfi");
        }
        u64 got = 0, busy = 0;
        for (u64 a = 0x40200000ULL; a < hi; a += 0x200000ULL) {
            u64 pa = a, np = (hi - a < 0x200000ULL ? hi - a : 0x200000ULL) / 0x1000ULL;
            if (((APG0) st->BootSvc->AllocatePages)(2 /*AllocateAddress*/, 2 /*EfiLoaderData*/, np,
                                                    &pa) == 0)
                got++;
            else
                busy++;
        }
        uputs("kernel window 0x40070000..");
        uhex(hi);
        uputs(": ");
        udec(got);
        uputs(" x 2 MiB reserved, ");
        udec(busy);
        uputs(" in use by the firmware or this loader\r\n");
    }

    /* A golden disc carries AI models: check each one's SHA-256 while the
     * firmware's file system is still there. Nothing happens on a plain disc. */
    zxai_rec_t *airec = 0;
    {
        typedef EFI_STATUS (*APG)(u32, u32, u64, u64 *);
        u64 ra = ZXAI_REC_ADDR_ARM64;
        if (((APG) st->BootSvc->AllocatePages)(2 /*AllocateAddress*/, 2 /*EfiLoaderData*/, 1,
                                               &ra) == 0) {
            airec = (zxai_rec_t *) ra;
            for (u64 i = 0; i < sizeof(*airec); i++) ((u8 *) airec)[i] = 0;
        } else {
            uputs("[AI_HANDOFF_NONE] record page 0x40071000 busy -- no boot model handoff\r\n");
        }
    }
    efi_check_models(image, st->BootSvc->HandleProtocol, st->BootSvc->AllocatePool,
                     st->BootSvc->FreePool, st->BootSvc->AllocatePages, st->BootSvc->FreePages,
                     ZXAI_MAX_ADDR_ARM64, airec);

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
    if(have_gop){
        *(volatile u64*)0x40070000ULL = 0x5A585642464F4F49ULL;  /* ZXV_BOOTINFO_MAGIC */
        *(volatile u64*)0x40070008ULL = fb;
        *(volatile u32*)0x40070010ULL = w;
        *(volatile u32*)0x40070014ULL = h;
        *(volatile u32*)0x40070018ULL = pps;
        *(volatile u32*)0x4007001CULL = pixfmt;
        uputs("handoff record published @0x40070000\r\n");
    } else {
        /* No GOP: explicitly clear the sentinel so a stale/garbage word at the
         * fixed bootinfo address can never be misread as the magic. The kernel's
         * magic check then fails and it takes ramfb (or stays on serial). */
        *(volatile u64*)0x40070000ULL = 0;
        uputs("no GOP handoff -- bootinfo sentinel cleared @0x40070000\r\n");
    }

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
    /* The boot model went into memory through the firmware's file reads with
     * the D-cache on; the kernel first reads it with the MMU (and so the
     * cache) off. Clean it to the point of coherency, as efi_tramp.S does for
     * the kernel and the record page. */
    if (airec && airec->magic == ZXAI_MAGIC) {
        u64 ctr;
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
        u64 line = 4ull << ((ctr >> 16) & 0xF);
        for (u64 a = airec->model_phys & ~(line - 1); a < airec->model_phys + airec->model_bytes;
             a += line)
            __asm__ __volatile__("dc cvac, %0" ::"r"(a) : "memory");
        __asm__ __volatile__("dsb sy" ::: "memory");
        uputs("boot model cleaned to memory for the kernel\r\n");
    }
    uputs(">>> handing off to ZXV kernel via trampoline (MMU off) <<<\r\n");
    ((void(*)(u64,u64,u64))tramp)(staging, 0x40080000ULL, ksize);
    for(;;) __asm__ __volatile__("wfi");
    return 0;
}
