/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
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
    EFI_GUID guid;
    void *table;
} CFGTAB;
typedef struct {
  char hdr[24]; void*FwVendor; u32 FwRev; void*CInH; void*ConIn; void*COutH; void*ConOut;
  void*CErrH; void*StdErr; void*RuntimeSvc; BOOTSVC *BootSvc;
  u64 NumberOfTableEntries;
  CFGTAB *ConfigurationTable;
} SYSTAB;
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

#include "../efi_models.h" /* golden disc: verify \ZXV\MODELS\*.gguf */
#include "../zxv_handoff.h" /* handoff v2: memory map, ACPI, clock, verdicts */

/* UEFI function-pointer types (all EFIAPI / ms_abi) */
typedef EFI_STATUS (EFIAPI *FN_GMM)(u64*, void*, u64*, u64*, u32*);
typedef EFI_STATUS (EFIAPI *FN_EBS)(EFI_HANDLE, u64);
typedef EFI_STATUS (EFIAPI *FN_POOL)(u32, u64, void**);
typedef EFI_STATUS (EFIAPI *FN_PAGES)(u32, u32, u64, u64*);
typedef EFI_STATUS(EFIAPI *FN_LOCATE)(EFI_GUID *, void *, void **);
typedef EFI_STATUS(EFIAPI *FN_WATCHDOG)(u64, u64, u64, void *);
typedef EFI_STATUS(EFIAPI *FN_GETTIME)(EFI_TIME *, void *);

/* Graphics Output Protocol, only the fields the handoff needs. */
typedef struct {
    u32 Ver, HRes, VRes, PixFmt, PixInfo[4], PixPerScan;
} GOP_INFO;
typedef struct {
    u32 MaxMode, Mode;
    GOP_INFO *Info;
    u64 SizeOfInfo, FrameBufferBase, FrameBufferSize;
} GOP_MODE;
typedef struct {
    void *QueryMode, *SetMode, *Blt;
    GOP_MODE *Mode;
} GOP;

/* Handoff record for the kernel's GOP text console (efifb_con.h). */
#define BOOTINFO_ADDR  0x80000ULL
#define BOOTINFO_MAGIC 0x5A585642464F4F49ULL

/* The asm trampoline: drops to 32-bit protected mode and jumps to `entry`. */
extern void x86_efi_handoff(u64 entry);

/* Embedded flat kernel image (efi_kernel_blob.S). */
extern u8 _kernel_blob_start[]; extern u8 _kernel_blob_end[];

#define KERNEL_LOAD 0x100000ULL   /* Multiboot1 load_addr / link base */

/* The kernel's full extent: image, BSS and boot stack (__stack_top, passed
 * in by Makefile.x86_64 from nm). The flat image is about 200 KB; reserving
 * only the file left the BSS and stack on memory firmware may still own.
 * 0 means "not known": fall back to the file size. */
#ifndef ZXV_KERNEL_END
#    define ZXV_KERNEL_END 0ULL
#endif

#define EFI_LOAD_ERROR (0x8000000000000000ULL | 1ULL)

static int guid_eq(const EFI_GUID *a, u32 x, u16 y, u16 z, const u8 *d)
{
    if (a->a != x || a->b != y || a->c != z) return 0;
    for (int i = 0; i < 8; i++)
        if (a->d[i] != d[i]) return 0;
    return 1;
}

/* Walk the final memory map: what does it say about overwriting [lo, hi)
 * once boot services end? Returns lpres numbering:
 *   1 TRUE   only conventional / boot-services memory (plus our own loader
 *            reservation when we hold one)
 *   3 BOTH   the window is otherwise free but covers ACPI memory (NVS or
 *            reclaimable tables). Booting still works, but ACPI there is
 *            destroyed: the stub sets a flag so the kernel holds every ACPI
 *            feature that would read it (sleep, AML, the RSDP when tables
 *            were hit) instead of trusting it.
 *   2 FALSE  runtime services, reserved, MMIO or someone else's loader
 *            memory: overwriting it breaks the firmware, so we refuse. */
static int window_by_map(const u8 *map, u64 msz, u64 dsz, u64 lo, u64 hi, int reserved, u8 *flags)
{
    int v = 1;
    for (u64 off = 0; off + dsz <= msz; off += dsz) {
        u32 type = *(const u32 *) (map + off);
        u64 b = *(const u64 *) (map + off + 8);
        u64 e = b + *(const u64 *) (map + off + 24) * 4096u;
        if (e <= lo || b >= hi) continue;
        u32 k = zxh_kind_of_efi(type);
        if (k == ZXH_MEM_USABLE) continue;
        if (k == ZXH_MEM_LOADER && reserved) continue;
        uputs("  window overlaps EFI type ");
        udec(type);
        uputs(" at ");
        udec(b);
        uputs("..");
        udec(e);
        uputs("\n");
        if (k == ZXH_MEM_ACPI_NVS) {
            *flags |= ZXH_F_ACPI_NVS_LOST;
            v |= 3;
        } else if (k == ZXH_MEM_ACPI_RECLAIM) {
            *flags |= ZXH_F_ACPI_TABLES_LOST;
            v |= 3;
        } else {
            return 2;
        }
    }
    return v;
}

EFIAPI unsigned long efi_main(EFI_HANDLE image, SYSTAB *st){
    serinit();
    uputs("\n>>> ZXV-EFI-STUB [x86_64]: efi_main under UEFI <<<\n");

    FN_PAGES AllocatePages   = (FN_PAGES)st->BootSvc->AllocatePages;
    FN_GMM   GetMemoryMap    = (FN_GMM)  st->BootSvc->GetMemoryMap;
    FN_EBS   ExitBootServices= (FN_EBS)  st->BootSvc->ExitBootServices;
    FN_POOL  AllocatePool    = (FN_POOL) st->BootSvc->AllocatePool;

    /* Firmware arms a 5-minute watchdog for boot loaders; hashing 1.6 GB of
     * models on slow media can outlast it. Disarm it: we are not hung. */
    ((FN_WATCHDOG) st->BootSvc->SetWatchdogTimer)(0, 0, 0, 0);

    /* The handoff v2 record lives in the stub's BSS until boot services end,
     * then is copied after the framebuffer record. */
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

    /* Find the firmware framebuffer while boot services still exist. Only
     * 32-bit RGBx/BGRx linear modes (PixFmt 0/1) can be drawn into directly. */
    u64 fb = 0;
    u32 w = 0, h = 0, pps = 0, pixfmt = 0;
    {
        EFI_GUID gop_guid = {
            0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
        GOP *gop = 0;
        FN_LOCATE LocateProtocol = (FN_LOCATE) st->BootSvc->LocateProtocol;
        EFI_STATUS gs = LocateProtocol(&gop_guid, 0, (void **) &gop);
        if (gs == 0 && gop && gop->Mode && gop->Mode->Info && gop->Mode->Info->PixFmt <= 1) {
            fb = gop->Mode->FrameBufferBase;
            w = gop->Mode->Info->HRes;
            h = gop->Mode->Info->VRes;
            pps = gop->Mode->Info->PixPerScan;
            pixfmt = gop->Mode->Info->PixFmt;
            uputs("GOP fb=");
            udec(fb);
            uputs(" res=");
            udec(w);
            uputc('x');
            udec(h);
            uputs(" stride=");
            udec(pps);
            uputs("\n");
        } else {
            uputs("no usable GOP (status=");
            udec(gs);
            uputs(") -- serial console only\n");
        }
    }

    u64 ksize = (u64)(_kernel_blob_end - _kernel_blob_start);
    u64 kend = ZXV_KERNEL_END > KERNEL_LOAD + ksize ? ZXV_KERNEL_END : KERNEL_LOAD + ksize;
    u64 kpages = (kend - KERNEL_LOAD + 0xFFF) / 0x1000;
    hx.kernel_lo = KERNEL_LOAD;
    hx.kernel_hi = KERNEL_LOAD + kpages * 0x1000;

    /* Reserve the kernel's whole window [0x100000, image + BSS) so nothing
     * firmware-owned sits there when we copy. AllocateAddress (type 2),
     * EfiLoaderData (2). This is the first of two witnesses (see window). */
    u64 addr = KERNEL_LOAD;
    EFI_STATUS as = AllocatePages(2 /*AllocateAddress*/, 2 /*EfiLoaderData*/, kpages, &addr);
    uputs("reserve 0x100000 status=");
    udec(as);
    uputs(" (");
    udec(ksize);
    uputs(" bytes image, ");
    udec(kpages * 0x1000);
    uputs(" bytes with BSS)\n");

    /* Reserve the handoff page, so no firmware or loader data sits there
     * when it is written. If the firmware already uses it, the kernel gets
     * no handoff at all (serial console, no v2 record). */
    int handoff = 1;
    {
        u64 bia = BOOTINFO_ADDR;
        if (AllocatePages(2 /*AllocateAddress*/, 2 /*EfiLoaderData*/, 1, &bia)) {
            uputs("handoff page 0x80000 busy -- serial console only, no handoff record\n");
            fb = 0;
            handoff = 0;
        }
    }

    /* A golden disc carries AI models: check each one's SHA-256 while the
     * firmware's file system is still there. Nothing happens on a plain disc. */
    int nmod = efi_check_models(image, st->BootSvc->HandleProtocol, st->BootSvc->AllocatePool,
                                st->BootSvc->FreePool);
    hx.models_bad = (u32) em_last_bad;
    hx.models_ok = nmod > em_last_bad ? (u32) (nmod - em_last_bad) : 0;
    hx.models = nmod == 0 ? 0 /*NEITHER*/ : (nmod < 0 || em_last_bad) ? 2 /*FALSE*/ : 1 /*TRUE*/;

    /* ---- GetMemoryMap + ExitBootServices (UEFI spec 7.4) ---- */
    u64 msz=0, mkey=0, dsz=0; u32 dver=0; void *mmap=0;
    GetMemoryMap(&msz,0,&mkey,&dsz,&dver);        /* sizing call (TOO_SMALL) */
    msz += 8*dsz;                                  /* headroom for map growth */
    AllocatePool(2 /*EfiLoaderData*/, msz, &mmap);
    GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);      /* real map + key */

    /* The window verdict: knowledge join of the reservation (TRUE, or
     * NEITHER: busy now says nothing about after ExitBootServices) and the
     * final map (TRUE only reclaimable memory, FALSE anything else). The
     * stub's own stack in the window is FALSE too: the copy would overwrite
     * it. Only TRUE boots; FALSE or BOTH returns to the firmware, which can
     * try the next boot option, instead of copying over memory we do not
     * own. */
    {
        u64 here = (u64) &msz;
        int w_alloc = as == 0 ? 1 : 0;
        int w_map = window_by_map((const u8 *) mmap, msz, dsz, hx.kernel_lo, hx.kernel_hi, as == 0,
                                  &hx.flags);
        if (here >= hx.kernel_lo && here < hx.kernel_hi) w_map = 2;
        /* FALSE from the map is final; otherwise join the two witnesses. */
        hx.window = (u8) (w_map == 2 ? 2 : (w_alloc | w_map));
        if (hx.window == 2) {
            uputs("[BOOT_REFUSED] kernel window overlaps memory the firmware keeps after "
                  "boot; not copying over it\n");
            return EFI_LOAD_ERROR;
        }
        if (hx.window == 3)
            uputs("[HANDOFF_HELD] kernel window covers ACPI memory: booting, ACPI held\n");
    }

    EFI_STATUS es = ExitBootServices(image, mkey);
    if(es){                                        /* key stale: refresh + retry */
        GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver);
        es = ExitBootServices(image, mkey);
    }
    uputs("ExitBootServices status="); udec(es); uputs("\n");
    if (es) {
        /* Boot services are still alive: hand control back rather than run
         * a kernel on a machine the firmware still believes it owns. */
        uputs("[BOOT_REFUSED] ExitBootServices failed twice\n");
        return EFI_LOAD_ERROR;
    }

    /* Compact the final map into the handoff record (address order as the
     * firmware gives it; adjacent regions of one kind merge). */
    for (u64 off = 0; off + dsz <= msz; off += dsz) {
        u8 *d = (u8 *) mmap + off;
        if (zxh_add_mem(&hx, *(u64 *) (d + 8), *(u64 *) (d + 24), zxh_kind_of_efi(*(u32 *) d))) {
            hx.flags |= ZXH_F_MEM_TRUNCATED;
            break;
        }
    }

    /* Boot services are gone, low memory is ours: publish the framebuffer
     * record (only when this boot found a GOP and owns the page). */
    {
        volatile u64 *bi = (volatile u64 *) BOOTINFO_ADDR;
        if (fb) {
            for (int i = 0; i < 8; i++) bi[i] = 0;
            bi[1] = fb;
            ((volatile u32 *) bi)[4] = w;
            ((volatile u32 *) bi)[5] = h;
            ((volatile u32 *) bi)[6] = pps;
            ((volatile u32 *) bi)[7] = pixfmt;
            bi[4] = BOOTINFO_MAGIC ^ fb ^ w ^ h ^ pps;
            bi[0] = BOOTINFO_MAGIC;
            uputs("GOP handoff record published @0x80000\n");
            hx.flags |= ZXH_F_GOP;
        }
        if (handoff) {
            zxh_seal(&hx);
            u8 *dst = (u8 *) (BOOTINFO_ADDR + ZXH_OFFSET);
            for (u64 i = 0; i < sizeof(hx); i++) dst[i] = ((const u8 *) &hx)[i];
            uputs("handoff v2 published: ");
            udec(hx.n_mem);
            uputs(" memory regions\n");
        }
    }

    /* Copy the kernel to its link address and hand off. */
    u8 *src = _kernel_blob_start, *dst = (u8*)KERNEL_LOAD;
    for(u64 i=0;i<ksize;i++) dst[i]=src[i];

    /* Multiboot1 header: entry_addr is the 8th dword (file offset 0x1C). */
    u64 entry = (u64)(*(volatile u32*)(KERNEL_LOAD + 0x1C));
    uputs("kernel copied; _entry32="); udec(entry); uputs("\n>>> handing off (long->PM32) <<<\n");

    x86_efi_handoff(entry);       /* never returns */
    for(;;) __asm__ __volatile__("hlt");
    return 0;
}
