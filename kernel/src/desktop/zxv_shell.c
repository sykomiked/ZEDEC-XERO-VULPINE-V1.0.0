/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_shell.c — the ZEDEC pqOS desktop shell (stateful, interactive). See zxv_shell.h. */
#include "zxv_shell.h"

/* ---- ZXV house palette (XRGB8888) ---- */
#define C_BAR      0x0F0F16u
#define C_BAR_HI   0x1B1B28u
#define C_GOLD     0xE6C158u
#define C_GOLD_DIM 0x8A7434u
#define C_RED      0x6E1417u
#define C_RED_DK   0x2A0B0Du
#define C_WINBODY  0x0A0A12u
#define C_TEXT     0xCED0DAu
#define C_DIM      0x7A7C8Au
#define C_GREEN    0x66DD88u
#define C_CYAN     0x62C6D6u
#define C_WHITE    0xF4F6FBu
#define C_BLACK    0x000000u

/* ---- dock app table (name, native file, glyph, accent) ---- */
static const char *APP_NAME[SHELL_APPS] = { "VINO","FLEET","STUDIO","CHIGLET","REFINERY" };
static const char *APP_FILE[SHELL_APPS] = { "WALLET.ZXVC","CREW.ZXVC","ART.ZXVC","MIND.ZXVC","SIGIL.ZXVC" };
static const char  APP_GLYPH[SHELL_APPS]= { 'V','F','S','C','R' };
static const uint32_t APP_ACC[SHELL_APPS]={ C_GOLD, C_RED, C_CYAN, C_GREEN, C_GOLD };

/* dock geometry — shared by hit-test and render */
#define DOCK_X 26
#define DOCK_Y 70
#define DOCK_S 76
#define DOCK_STEP 118

/* ---- tiny string helpers (no libc) ---- */
static uint32_t slen(const char *s){ uint32_t n=0; while(s[n]) n++; return n; }
static void scopy(char *d, const char *s, uint32_t cap){ uint32_t i=0; for(; s[i] && i<cap-1; i++) d[i]=s[i]; d[i]=0; }
static char upc(char c){ return (c>='a'&&c<='z')?(char)(c-32):c; }
static int seq_up(const char *a, const char *b){ /* a==b, case-insensitive */
    uint32_t i=0; for(;;){ char ca=upc(a[i]), cb=upc(b[i]); if(ca!=cb) return 0; if(!ca) return 1; i++; }
}

/* ---- drawing primitives ---- */
static void box(vbe_state_t *v,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c){ vbe_fill_rect(v,x,y,w,h,c); }
static void frame(vbe_state_t *v,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c){
    vbe_fill_rect(v,x,y,w,1,c); vbe_fill_rect(v,x,y+h-1,w,1,c);
    vbe_fill_rect(v,x,y,1,h,c); vbe_fill_rect(v,x+w-1,y,1,h,c);
}
static void gt(vbe_state_t *v,int32_t x,int32_t y,const char *s,uint32_t fg,int32_t sc){ vbe_draw_text_ex(v,x,y,s,fg,0,sc,0); }

static int32_t window(vbe_state_t *v,int32_t x,int32_t y,int32_t w,int32_t h,const char *title,uint32_t accent){
    box(v,x+6,y+6,w,h,0x05050A);
    box(v,x,y,w,h,C_WINBODY);
    frame(v,x,y,w,h,accent);
    box(v,x+1,y+1,w-2,24,C_RED_DK);
    vbe_fill_rect(v,x+1,y+25,w-2,1,C_GOLD_DIM);
    gt(v,x+12,y+5,title,C_GOLD,1);
    box(v,x+w-20,y+9,8,8,C_GREEN); box(v,x+w-34,y+9,8,8,C_GOLD); box(v,x+w-48,y+9,8,8,C_RED);
    return y+34;
}
static void icon(vbe_state_t *v,int32_t x,int32_t y,char glyph,uint32_t accent,const char *name,const char *file,int active){
    box(v,x,y,DOCK_S,DOCK_S, active?0x161020u:C_WINBODY);
    frame(v,x,y,DOCK_S,DOCK_S,accent);
    if(active){ frame(v,x-2,y-2,DOCK_S+4,DOCK_S+4,accent); frame(v,x-1,y-1,DOCK_S+2,DOCK_S+2,C_GOLD); }
    else frame(v,x+1,y+1,DOCK_S-2,DOCK_S-2,C_BAR_HI);
    char g[2]={glyph,0};
    gt(v,x+DOCK_S/2-12,y+DOCK_S/2-24,g,accent,3);
    gt(v,x+6,y+DOCK_S+4,name,active?C_GOLD:C_TEXT,1);
    gt(v,x-4,y+DOCK_S+20,file,C_GOLD_DIM,1);
}
static void cursor(vbe_state_t *v,int32_t x,int32_t y){
    static const char *A[]={"1","11","121","1221","12221","122221","1222221","12222221",
        "122222221","1222222221","12222111111","122122","1221 12","121  12","11    122","      12"};
    for(int32_t r=0;r<16;r++) for(int32_t c=0;A[r][c];c++){
        if(A[r][c]=='1') vbe_set_pixel(v,x+c,y+r,C_BLACK);
        else if(A[r][c]=='2') vbe_set_pixel(v,x+c,y+r,C_WHITE);
    }
}

/* ---- terminal scrollback ---- */
static void term_push(zxv_shell_state_t *st, const char *line){
    if(st->term_count < SHELL_TERM_LINES){
        scopy(st->term[st->term_count], line, SHELL_LINE_LEN); st->term_count++;
    } else {
        for(int i=1;i<SHELL_TERM_LINES;i++) scopy(st->term[i-1], st->term[i], SHELL_LINE_LEN);
        scopy(st->term[SHELL_TERM_LINES-1], line, SHELL_LINE_LEN);
    }
}
static void term_prompt_echo(zxv_shell_state_t *st){
    char buf[SHELL_LINE_LEN]; const char *p="VULPINE@ZXV:~$ "; uint32_t i=0,j=0;
    while(p[i] && j<SHELL_LINE_LEN-1) buf[j++]=p[i++];
    for(int k=0;k<st->cmdlen && j<SHELL_LINE_LEN-1;k++) buf[j++]=st->cmd[k];
    buf[j]=0; term_push(st,buf);
}

/* run the current command line */
static void run_cmd(zxv_shell_state_t *st){
    term_prompt_echo(st);
    st->cmd[st->cmdlen]=0;
    const char *c = st->cmd;
    if(st->cmdlen==0){ /* nothing */ }
    else if(seq_up(c,"HELP")){
        term_push(st,"COMMANDS: LS  OPEN  HELP  CLEAR");
        term_push(st,"APPS: VINO FLEET STUDIO CHIGLET REFINERY");
    } else if(seq_up(c,"LS")){
        term_push(st,"WALLET.ZXVC  CREW.ZXVC  ART.ZXVC  MIND.ZXVC  SIGIL.ZXVC");
        term_push(st,"  (each carries .CEDEZ [S-] and .CEDEC [S0])");
    } else if(seq_up(c,"CLEAR")){
        st->term_count=0;
    } else if(seq_up(c,"OPEN")){
        term_push(st,"USAGE: OPEN <FILE.ZXVC>   e.g. OPEN WALLET.ZXVC");
    } else {
        int hit=-1;
        for(int i=0;i<SHELL_APPS;i++) if(seq_up(c,APP_NAME[i])) { hit=i; break; }
        if(hit<0) for(int i=0;i<SHELL_APPS;i++) if(seq_up(c,APP_FILE[i])) { hit=i; break; }
        if(hit>=0){
            char ln[SHELL_LINE_LEN]; const char *a="OPENED "; uint32_t j=0;
            while(a[j] && j<20) { ln[j]=a[j]; j++; }
            const char *nm=APP_NAME[hit]; uint32_t k=0; while(nm[k]&&j<SHELL_LINE_LEN-14) ln[j++]=nm[k++];
            const char *tail=" [S+] VERIFIED"; k=0; while(tail[k]&&j<SHELL_LINE_LEN-1) ln[j++]=tail[k++];
            ln[j]=0; term_push(st,ln);
            st->active_app=hit;
        } else {
            char ln[SHELL_LINE_LEN]; const char *a="NOT FOUND: "; uint32_t j=0;
            while(a[j]&&j<20){ln[j]=a[j];j++;}
            for(int k=0;k<st->cmdlen&&j<SHELL_LINE_LEN-12;k++) ln[j++]=st->cmd[k];
            const char *t=" (TRY HELP)"; uint32_t k=0; while(t[k]&&j<SHELL_LINE_LEN-1) ln[j++]=t[k++];
            ln[j]=0; term_push(st,ln);
        }
    }
    st->cmdlen=0; st->cmd[0]=0;
}

/* Linux evdev keycode -> lowercase ASCII (0 = ignore) */
static char keymap(int32_t k){
    static const char row[64]={
    /*0*/ 0,0,'1','2','3','4','5','6','7','8','9','0','-',0,'\b',0,
    /*16*/ 'q','w','e','r','t','y','u','i','o','p',0,0,'\n',0,'a','s',
    /*32*/ 'd','f','g','h','j','k','l',0,0,0,0,0,'z','x','c','v',
    /*48*/ 'b','n','m',0,'.','/',0,0,' ',0,0,0,0,0,0,0 };
    if(k<0||k>=64) return 0; return row[k];
}

void zxv_shell_init(zxv_shell_state_t *st){
    st->active_app=-1; st->prev_buttons=0; st->term_count=0; st->cmdlen=0; st->cmd[0]=0; st->click_count=0;
    term_push(st,"ZEDEC XERO VULPINE  --  pqOS [ARM64]");
    term_push(st,"THE KERNEL IS THE OS: ECONOMY, GOVERNANCE, SOCIAL IN-KERNEL");
    term_push(st,"CLICK A DOCK APP, OR TYPE A COMMAND. TRY: HELP");
}

void zxv_shell_key(zxv_shell_state_t *st, int32_t keycode){
    char c=keymap(keycode);
    if(!c) return;
    if(c=='\n') run_cmd(st);
    else if(c=='\b'){ if(st->cmdlen>0) st->cmdlen--; }
    else if(st->cmdlen < SHELL_CMD_MAX-1){ st->cmd[st->cmdlen++]=c; }
}

/* hit-test a left-click edge against the dock */
static void handle_click(zxv_shell_state_t *st, int32_t cx, int32_t cy){
    for(int i=0;i<SHELL_APPS;i++){
        int32_t y=DOCK_Y+i*DOCK_STEP;
        if(cx>=DOCK_X && cx<DOCK_X+DOCK_S && cy>=y && cy<y+DOCK_S){
            st->active_app=i;
            /* launching = echo an open into the terminal */
            char ln[SHELL_LINE_LEN]; const char *a="VULPINE@ZXV:~$ OPEN "; uint32_t j=0;
            while(a[j]&&j<30){ln[j]=a[j];j++;}
            const char *f=APP_FILE[i]; uint32_t k=0; while(f[k]&&j<SHELL_LINE_LEN-1) ln[j++]=f[k++]; ln[j]=0;
            term_push(st,ln);
            char ln2[SHELL_LINE_LEN]; const char *b="  OPENED "; j=0; while(b[j]&&j<20){ln2[j]=b[j];j++;}
            const char *nm=APP_NAME[i]; k=0; while(nm[k]&&j<SHELL_LINE_LEN-14) ln2[j++]=nm[k++];
            const char *t=" [S+] VERIFIED"; k=0; while(t[k]&&j<SHELL_LINE_LEN-1) ln2[j++]=t[k++]; ln2[j]=0;
            term_push(st,ln2);
            return;
        }
    }
    /* START button (taskbar, left) */
    /* handled in frame via H; left here for dock focus only */
}

void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy, uint32_t buttons, uint32_t tick){
    const int32_t W=(int32_t)v->width, H=(int32_t)v->height;

    /* --- input: left-click edge --- */
    if((buttons & 1u) && !(st->prev_buttons & 1u)){
        st->click_count++;
        if(cy < H-32) handle_click(st,cx,cy);          /* desktop area */
        else {                                          /* taskbar */
            if(cx>=8 && cx<82) term_push(st,"ZEDEC MENU: 5 APPS  ONE POLICY  PHASE-TICK");
        }
    }
    st->prev_buttons=buttons;

    /* --- menu bar --- */
    box(v,0,0,W,40,C_BAR); vbe_fill_rect(v,0,39,W,1,C_GOLD_DIM);
    box(v,12,8,24,24,C_GOLD); box(v,17,13,14,14,C_RED);
    gt(v,48,4,"ZEDEC PQOS",C_GOLD,2);
    gt(v,W-470,4,"ZEDEC XERO VULPINE  --  POST-QUANTUM OS",C_DIM,1);
    gt(v,W-470,21,"ECONOMY 5/5   PLATFORM 8/8   EL0   PHASE-TICK",C_GOLD_DIM,1);

    /* --- dock --- */
    for(int i=0;i<SHELL_APPS;i++)
        icon(v,DOCK_X,DOCK_Y+i*DOCK_STEP,APP_GLYPH[i],APP_ACC[i],APP_NAME[i],APP_FILE[i], st->active_app==i);

    /* --- terminal window (live) --- */
    {
        int32_t x=176,y=70,w=660,h=566;
        int32_t ty=window(v,x,y,w,h,"ZEDEC TERMINAL  --  VULPINE@ZXV:~",C_GOLD);
        int32_t tx=x+14;
        for(int i=0;i<st->term_count;i++){ gt(v,tx,ty,st->term[i],(i<3?C_DIM:C_TEXT),1); ty+=18; }
        /* live prompt */
        gt(v,tx,ty,"VULPINE@ZXV:~$ ",C_GREEN,1);
        int32_t px=tx+8*15;
        { char b[SHELL_CMD_MAX+1]; scopy(b,st->cmd,SHELL_CMD_MAX+1); gt(v,px,ty,b,C_TEXT,1); }
        if((tick/8)&1u) box(v,px+8*st->cmdlen,ty,9,15,C_GREEN);
    }

    /* --- Chiglet panel --- */
    {
        int32_t x=858,y=70,w=396,h=250; int32_t ty=window(v,x,y,w,h,"CHIGLET -- AI COMPANION",C_GREEN);
        int32_t tx=x+14;
        gt(v,tx,ty,"NATIVE MIXTURE-OF-EXPERTS",C_DIM,1); ty+=24;
        gt(v,tx,ty,"> MATCH TROLLS WITH TROLLS",C_TEXT,1); ty+=18;
        gt(v,tx,ty,"> CONCORD: REHABILITATIVE",C_TEXT,1); ty+=18;
        gt(v,tx,ty,"> PIG BADGE: OVER 9000",C_GOLD,1); ty+=26;
        gt(v,tx,ty,"R (DISTINCT EVIDENCE): 3.0",C_CYAN,1); ty+=18;
        gt(v,tx,ty,"VERDICT: DECIDED",C_GREEN,1);
    }
    /* --- Vino wallet panel --- */
    {
        int32_t x=858,y=336,w=396,h=300; int32_t ty=window(v,x,y,w,h,"VINO -- FLOATING VOUCHERS",C_GOLD);
        int32_t tx=x+14;
        gt(v,tx,ty,"TRIPLE RAIL (ISO 4217)",C_DIM,1); ty+=24;
        gt(v,tx,ty,"846 DEBIT    120.00",C_TEXT,1); ty+=18;
        gt(v,tx,ty,"888 CREDIT    12.00",C_TEXT,1); ty+=18;
        gt(v,tx,ty,"999 EQUITY   108.00",C_GREEN,1); ty+=26;
        gt(v,tx,ty,"COVERAGE 1.8X  SOLVENT",C_CYAN,1); ty+=18;
        gt(v,tx,ty,"NO DEBT, MERIT-BASED",C_DIM,1); ty+=26;
        gt(v,tx,ty,"[SEND]  [SWAP]  [REDEEM]",C_GOLD,1);
    }

    /* --- taskbar --- */
    box(v,0,H-32,W,32,C_BAR); vbe_fill_rect(v,0,H-32,W,1,C_GOLD_DIM);
    box(v,8,H-26,74,20,C_RED_DK); frame(v,8,H-26,74,20,C_GOLD_DIM);
    gt(v,16,H-24,"START",C_GOLD,1);
    for(int i=0;i<SHELL_APPS;i++) gt(v,98+i*74,H-24,APP_NAME[i], st->active_app==i?C_GOLD:C_DIM,1);
    {
        char buf[20]; uint32_t t=tick; char tmp[12]; int i=0;
        if(t==0) tmp[i++]='0'; while(t>0&&i<10){tmp[i++]=(char)('0'+(t%10));t/=10;}
        int j=0; const char *p="TICK "; while(*p) buf[j++]=*p++; while(i>0) buf[j++]=tmp[--i]; buf[j]=0;
        gt(v,W-110,H-24,buf,C_CYAN,1);
    }

    /* --- pointer --- */
    cursor(v,cx,cy);
}
