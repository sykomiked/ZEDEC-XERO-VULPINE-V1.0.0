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
  u64 NumberOfTableEntries;
  struct CFGTAB *ConfigurationTable;
} SYSTAB;
typedef struct CFGTAB {
    EFI_GUID guid;
    void *table;
} CFGTAB;
typedef struct {
    char hdr[24];
    void *GetTime;
} RTSVC;
typedef struct {
    u16 Year;
    u8 Month, Day, Hour, Minute, Second, Pad1;
    u32 Nanosecond;
    short TimeZone;
    u8 Daylight, Pad2;
} EFI_TIME;

#define EFIAPI             /* AArch64 UEFI calls use the plain AAPCS64 ABI */
#include "../efi_models.h" /* golden disc: verify \ZXV\MODELS\*.gguf */
#include "../zxv_handoff.h" /* handoff v2: memory map, ACPI, clock, verdicts */

#define KERNEL_LOAD    0x40080000ULL
#define BOOTINFO_ADDR  0x40070000ULL /* zxv_bootinfo.h; handoff v2 at +ZXH_OFFSET */
#define EFI_LOAD_ERROR (0x8000000000000000ULL | 1ULL)

typedef EFI_STATUS (*FN_GETTIME)(EFI_TIME *, void *);
typedef EFI_STATUS (*FN_WATCHDOG)(u64, u64, u64, void *);

static int guid_eq(const EFI_GUID *a, u32 x, u16 y, u16 z, const u8 *d)
{
    if (a->a != x || a->b != y || a->c != z) return 0;
    for (int i = 0; i < 8; i++)
        if (a->d[i] != d[i]) return 0;
    return 1;
}

static int overlaps(u64 a, u64 alen, u64 b, u64 blen)
{
    return a < b + blen && b < a + alen;
}

unsigned long efi_main(EFI_HANDLE image, SYSTAB *st)
{
    uputs("\r\n>>> ZXV-EFI-STUB: efi_main under UEFI <<<\r\n");

    /* Firmware arms a 5-minute watchdog for boot loaders; hashing 1.6 GB of
     * models on slow media can outlast it. Disarm it: we are not hung. */
    ((FN_WATCHDOG) st->BootSvc->SetWatchdogTimer)(0, 0, 0, 0);

    /* Handoff v2 (zxv_handoff.h), as on x86_64: built in the stub's BSS,
     * copied after the framebuffer record once boot services end. */
    static zxh_t hx;
    for (u64 i = 0; i < sizeof(hx); i++) ((u8 *) &hx)[i] = 0;
    {
        static const u8 acpi20_d[8] = {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81};
        static const u8 acpi10_d[8] = {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b};
        static const u8 smbios3_d[8] = {0xb2, 0x71, 0x37, 0xe8, 0x6e, 0x33, 0x0d, 0x91};
        for (u64 i = 0; i < st->NumberOfTableEntries; i++) {
            CFGTAB *c = &st->ConfigurationTable[i];
            if (guid_eq(&c->guid, 0x8868e871, 0xe4f1, 0x11d3, acpi20_d))
                hx.rsdp = (u64) c->table;
            else if (!hx.rsdp && guid_eq(&c->guid, 0xeb9d2d30, 0x2d88, 0x11d3, acpi10_d))
                hx.rsdp = (u64) c->table;
            else if (guid_eq(&c->guid, 0xf2fd1544, 0x9794, 0x4a2c, smbios3_d))
                hx.smbios3 = (u64) c->table;
        }
        EFI_TIME t;
        RTSVC *rt = (RTSVC *) st->RuntimeSvc;
        if (rt && ((FN_GETTIME) rt->GetTime)(&t, 0) == 0)
            hx.efi_time = zxh_efi_time_to_unix(t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second,
                                               t.TimeZone);
    }
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
    int nmod = efi_check_models(image, st->BootSvc->HandleProtocol, st->BootSvc->AllocatePool,
                                st->BootSvc->FreePool, st->BootSvc->AllocatePages,
                                st->BootSvc->FreePages, ZXAI_MAX_ADDR_ARM64, airec);
    hx.models_bad = (u32) em_last_bad;
    hx.models_ok = nmod > em_last_bad ? (u32) (nmod - em_last_bad) : 0;
    /* TRUE only when every listed model was checked and matched; models left
     * on the swarm partition keep it NEITHER unless one checked was bad. */
    hx.models = (nmod < 0 || em_last_bad)         ? 2 /*FALSE*/
                : (nmod > 0 && !em_last_deferred) ? 1 /*TRUE*/
                                                  : 0 /*NEITHER*/;

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

    u64 msz=0, mkey=0, dsz=0; u32 dver=0; void *mmap=0;
    GetMemoryMap(&msz,0,&mkey,&dsz,&dver);      /* sizing call (returns TOO_SMALL) */
    msz += 4*dsz;                                /* headroom for map growth        */
    AllocatePool(2 /*EfiLoaderData*/, msz, &mmap);
    GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);    /* real map + key                 */

    /* The window [handoff page, kernel image + BSS) is overwritten after
     * ExitBootServices. AAVMF loads this stub inside it, so the stub cannot
     * reserve it the way the x86_64 stub does; the final map is the witness
     * (zxh_window_of_map: loader memory there is ours, the stub and its
     * pool, and is allowed). Two things the map cannot see are checked
     * directly: the staged kernel and the trampoline must lie outside the
     * window (the copy would overwrite its own source), and the handoff page
     * must not hold this stub, its record or the map (writing the record
     * would corrupt them). FALSE returns to the firmware. */
    u64 kend = ZXV_KERNEL_END > KERNEL_LOAD + ksize ? ZXV_KERNEL_END : KERNEL_LOAD + ksize;
    hx.kernel_lo = KERNEL_LOAD;
    hx.kernel_hi = (kend + 0xFFF) & ~0xFFFULL;
    int handoff = 1;
    {
        u64 win_lo = BOOTINFO_ADDR, win_len = hx.kernel_hi - BOOTINFO_ADDR;
        u64 kpages = (ksize + 0xFFF) / 0x1000;
        u32 wt = 0;
        u64 wb = 0;
        int wv = zxh_window_of_map((const u8 *) mmap, msz, dsz, win_lo, hx.kernel_hi, 1, &hx.flags,
                                   &wt, &wb);
        if (wv != 1) {
            uputs("  window overlaps EFI type ");
            udec(wt);
            uputs(" at ");
            uhex(wb);
            uputs("\r\n");
        }
        if (overlaps(staging, kpages * 0x1000, win_lo, win_len) ||
            overlaps(tramp, 0x1000, win_lo, win_len)) {
            uputs("  staged kernel or trampoline inside the kernel window\r\n");
            wv = 2;
        }
        hx.window = (u8) wv;
        if (wv == 2) {
            uputs("[BOOT_REFUSED] kernel window overlaps memory the firmware keeps after "
                  "boot; not copying over it\r\n");
            return EFI_LOAD_ERROR;
        }
        if (wv == 3)
            uputs("[HANDOFF_HELD] kernel window covers ACPI memory: booting, ACPI held\r\n");
        u64 here = (u64) (unsigned long) &efi_main, rec = (u64) (unsigned long) &hx;
        if (overlaps(here, 1, BOOTINFO_ADDR, 0x1000) ||
            overlaps(rec, sizeof(hx), BOOTINFO_ADDR, 0x1000) ||
            overlaps((u64) (unsigned long) mmap, msz, BOOTINFO_ADDR, 0x1000)) {
            uputs("handoff page 0x40070000 holds the stub -- no handoff record\r\n");
            handoff = 0;
        }
    }

    EFI_STATUS es = ExitBootServices(image, mkey);
    if(es){                                      /* map moved: refresh key, retry  */
        GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);
        es = ExitBootServices(image, mkey);
    }
    if (es) {
        /* Boot services are still alive: hand control back rather than run
         * a kernel on a machine the firmware still believes it owns. */
        uputs("[BOOT_REFUSED] ExitBootServices failed twice\r\n");
        return EFI_LOAD_ERROR;
    }
    /* Boot services are gone. The PL011 UART @0x09000000 still works (bare metal). */
    uputs("ExitBootServices ok\r\n");

    /* Compact the final map into the record (adjacent regions of one kind
     * merge), then publish both records 64 KB below the kernel: the GOP
     * framebuffer record (zxv_bootinfo_t) and handoff v2 right after it. The
     * kernel adopts the GOP fb when the first magic is present; with no GOP
     * the sentinel is cleared so a stale word is never read as the magic. */
    for (u64 off = 0; off + dsz <= msz; off += dsz) {
        u8 *d = (u8 *) mmap + off;
        if (zxh_add_mem(&hx, *(u64 *) (d + 8), *(u64 *) (d + 24), zxh_kind_of_efi(*(u32 *) d))) {
            hx.flags |= ZXH_F_MEM_TRUNCATED;
            break;
        }
    }
    if (handoff) {
        if (have_gop) {
            *(volatile u64 *) 0x40070000ULL = 0x5A585642464F4F49ULL; /* ZXV_BOOTINFO_MAGIC */
            *(volatile u64 *) 0x40070008ULL = fb;
            *(volatile u32 *) 0x40070010ULL = w;
            *(volatile u32 *) 0x40070014ULL = h;
            *(volatile u32 *) 0x40070018ULL = pps;
            *(volatile u32 *) 0x4007001CULL = pixfmt;
            hx.flags |= ZXH_F_GOP;
            uputs("handoff record published @0x40070000\r\n");
        } else {
            *(volatile u64 *) 0x40070000ULL = 0;
            uputs("no GOP handoff -- bootinfo sentinel cleared @0x40070000\r\n");
        }
        zxh_seal(&hx);
        u8 *dst = (u8 *) (BOOTINFO_ADDR + ZXH_OFFSET);
        for (u64 i = 0; i < sizeof(hx); i++) dst[i] = ((const u8 *) &hx)[i];
        uputs("handoff v2 published: ");
        udec(hx.n_mem);
        uputs(" memory regions\r\n");
    }

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
