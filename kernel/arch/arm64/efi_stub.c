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
    uputs("GOP fb="); uhex(fb); uputs(" res="); udec(w); uputc('x'); udec(h);
    uputs(" stride="); udec(pps); uputs("\r\n");
    /* fill the framebuffer: ZXV gold-ish gradient with a red bar — proves we can draw */
    volatile u32 *p=(volatile u32*)fb;
    for(u32 y=0;y<h;y++) for(u32 x=0;x<w;x++){
        u32 c = ((y*0xE6/h)<<16)|((y*0xC1/h)<<8)|0x16;      /* gold gradient (XRGB) */
        if(y<40 || y>h-40) c=0x6E1417;                       /* red bars top/bottom */
        p[y*pps+x]=c;
    }
    uputs("GOP: framebuffer filled. Spinning.\r\n");
    for(;;) __asm__ __volatile__("wfi");
    return 0;
}
