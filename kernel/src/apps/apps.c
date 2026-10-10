/* apps.c — ZEDEC pqOS Native Applications Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "apps.h"

static int str_len(const char *s){int n=0;while(s[n])n++;return n;}
static int str_cmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return(int)(unsigned char)*a-(int)(unsigned char)*b;}
static void str_copy(char*d,const char*s){int i=0;while(s[i]){d[i]=s[i];i++;}d[i]=0;}
static void mem_set(void*d,int c,uint32_t n){uint8_t*p=d;for(uint32_t i=0;i<n;i++)p[i]=(uint8_t)c;}

void app_clear_screen(char*s,uint32_t mr,uint32_t mc){for(uint32_t r=0;r<mr;r++)for(uint32_t c=0;c<mc;c++)s[r*(mc+1)+c]=' ';for(uint32_t r=0;r<mr;r++)s[r*(mc+1)+mc]=0;}
void app_put_char(char*s,uint32_t r,uint32_t c,uint32_t mr,uint32_t mc,char ch){if(r>=mr||c>=mc)return;s[r*(mc+1)+c]=ch;}
void app_put_str(char*s,uint32_t r,uint32_t c,uint32_t mr,uint32_t mc,const char*str){while(*str&&c<mc)app_put_char(s,r,c,mr,mc,*str),str++,c++;}
void app_scroll_up(char*s,uint32_t mr,uint32_t mc){for(uint32_t r=0;r<mr-1;r++)for(uint32_t c=0;c<=mc;c++)s[r*(mc+1)+c]=s[(r+1)*(mc+1)+c];for(uint32_t c=0;c<mc;c++)s[(mr-1)*(mc+1)+c]=' ';s[(mr-1)*(mc+1)+mc]=0;}

static void num_str(uint32_t v,char*buf){char tmp[12];int n=0;if(v==0)buf[n++]='0';while(v>0){tmp[n++]='0'+(v%10);v/=10;}for(int j=n-1;j>=0;j--)buf[n-1-j]=tmp[j];buf[n]=0;}

/* ---- Shell ---- */
static void shell_print(shell_app_t*a,const char*s){
while(*s){if(*s=='\n'){a->cursor_col=0;a->cursor_row++;if(a->cursor_row>=APP_SHELL_LINES){app_scroll_up((char*)a->screen,APP_SHELL_LINES,APP_SHELL_COLS);a->cursor_row=APP_SHELL_LINES-1;}s++;continue;}
app_put_char((char*)a->screen,a->cursor_row,a->cursor_col,APP_SHELL_LINES,APP_SHELL_COLS,*s);a->cursor_col++;
if(a->cursor_col>=APP_SHELL_COLS){a->cursor_col=0;a->cursor_row++;if(a->cursor_row>=APP_SHELL_LINES){app_scroll_up((char*)a->screen,APP_SHELL_LINES,APP_SHELL_COLS);a->cursor_row=APP_SHELL_LINES-1;}}s++;}}
static void shell_prompt(shell_app_t*a){shell_print(a,"ZEDEC> ");}

static void shell_help(shell_app_t*a){shell_print(a,"ZEDEC pqOS Shell:\n  help  ls  cd  cat  echo  ps  net  route  vino  vena  clear  ver\n");}
static void shell_ver(shell_app_t*a){shell_print(a,"ZEDEC pqOS v1.0 M5 Axiomatic Kernel\nVOVINA SHAKINA build\n");}
static void shell_ps(shell_app_t*a){
if(!a->sched){shell_print(a,"No scheduler\n");return;}
shell_print(a,"PID NAME           STATE\n");
char line[80];
for(uint32_t i=0;i<a->sched->num_tasks;i++){task_t*t=&a->sched->tasks[i];if(t->state==TASK_UNUSED)continue;
int p=0;line[p++]='0'+(t->id/10);line[p++]='0'+(t->id%10);line[p++]=' ';
for(int j=0;j<15&&t->name[j];j++)line[p++]=t->name[j];while(p<18)line[p++]=' ';
const char*sn="???";if(t->state==TASK_READY)sn="READY";else if(t->state==TASK_RUNNING)sn="RUNNING";else if(t->state==TASK_BLOCKED)sn="BLOCKED";
for(int j=0;sn[j]&&p<27;j++)line[p++]=sn[j];line[p]=0;shell_print(a,line);shell_print(a,"\n");}}

static void shell_net(shell_app_t*a){
if(!a->net){shell_print(a,"No network\n");return;}
shell_print(a,"Interfaces:\n");
for(uint32_t i=0;i<a->net->num_interfaces;i++){shell_print(a,"  ");shell_print(a,a->net->interfaces[i].name);shell_print(a,a->net->interfaces[i].up?" [UP]\n":" [DOWN]\n");}
char b[32];num_str(a->net->rx_packets,b);shell_print(a,"RX pkts: ");shell_print(a,b);shell_print(a,"\n");}

static void shell_route(shell_app_t*a){
if(!a->router){shell_print(a,"No router\n");return;}
shell_print(a,"M5 Omni-Router Adapters:\n");
for(uint32_t i=0;i<M5_PROTO_MAX;i++)if(a->router->adapters[i].active){shell_print(a,"  [");shell_print(a,m5_proto_name((m5_proto_t)i));shell_print(a,"]\n");}
char b[32];num_str(a->router->num_routes,b);shell_print(a,"Routes: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->router->phone_map_count,b);shell_print(a,"Phone mappings: ");shell_print(a,b);shell_print(a,"\n");}

static void shell_vino(shell_app_t*a){
if(!a->vino){shell_print(a,"No Vino\n");return;}
char b[32];
num_str(a->vino->num_accounts,b);shell_print(a,"Accounts: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vino->num_txns,b);shell_print(a,"Transactions: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vino->num_assets,b);shell_print(a,"Assets: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vino->num_peers,b);shell_print(a,"Peers: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vino->block_height,b);shell_print(a,"Block: ");shell_print(a,b);shell_print(a,"\n");
shell_print(a,"Validator: ");shell_print(a,a->vino->is_validator?"YES":"NO");shell_print(a,"\n");}

static void shell_vena(shell_app_t*a){
if(!a->vena){shell_print(a,"No Vena\n");return;}
char b[32];
num_str(a->vena->num_contracts,b);shell_print(a,"Contracts: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vena->num_apps,b);shell_print(a,"Apps: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vena->active_apps,b);shell_print(a,"Active: ");shell_print(a,b);shell_print(a,"\n");
num_str(a->vena->num_oracles,b);shell_print(a,"Oracles: ");shell_print(a,b);shell_print(a,"\n");
shell_print(a,"Language: ");shell_print(a,vena_language_name(a->vena->default_language));shell_print(a,"\n");}

void shell_init(shell_app_t*a,uint32_t gw,vfs_state_t*vfs,net_state_t*net,m5_router_t*router,vino_ledger_t*vino,vena_runtime_t*vena,scheduler_t*sched){
mem_set(a,0,sizeof(*a));a->base.type=APP_SHELL;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"Shell");
a->vfs=vfs;a->net=net;a->router=router;a->vino=vino;a->vena=vena;a->sched=sched;
app_clear_screen((char*)a->screen,APP_SHELL_LINES,APP_SHELL_COLS);a->cursor_row=0;a->cursor_col=0;
shell_print(a,"ZEDEC pqOS Shell v1.0\nType 'help' for commands.\n\n");shell_prompt(a);}

void shell_execute(shell_app_t*a){
a->input_line[a->input_len]=0;
if(a->input_len>0&&a->hist_count<APP_SHELL_HISTORY){str_copy(a->history[a->hist_count],a->input_line);a->hist_count++;}
a->hist_pos=a->hist_count;shell_print(a,a->input_line);shell_print(a,"\n");
char*cmd=a->input_line;char*arg=0;
for(int i=0;cmd[i];i++){if(cmd[i]==' '){cmd[i]=0;arg=&cmd[i+1];break;}}
if(str_cmp(cmd,"help")==0||str_cmp(cmd,"?")==0)shell_help(a);
else if(str_cmp(cmd,"ver")==0)shell_ver(a);
else if(str_cmp(cmd,"clear")==0||str_cmp(cmd,"cls")==0){app_clear_screen((char*)a->screen,APP_SHELL_LINES,APP_SHELL_COLS);a->cursor_row=0;a->cursor_col=0;}
else if(str_cmp(cmd,"echo")==0){if(arg)shell_print(a,arg);shell_print(a,"\n");}
else if(str_cmp(cmd,"ps")==0)shell_ps(a);
else if(str_cmp(cmd,"net")==0)shell_net(a);
else if(str_cmp(cmd,"route")==0)shell_route(a);
else if(str_cmp(cmd,"vino")==0)shell_vino(a);
else if(str_cmp(cmd,"vena")==0)shell_vena(a);
else if(str_cmp(cmd,"ls")==0){if(a->vfs){vfs_node_t e[20];const char*p=arg?arg:vfs_get_cwd(a->vfs);int32_t n=vfs_list_dir(a->vfs,p,e,20);if(n>0)for(int32_t i=0;i<n;i++){shell_print(a,e[i].is_dir?"[D] ":"    ");shell_print(a,e[i].name);shell_print(a,"\n");}}}
else if(str_cmp(cmd,"cd")==0){if(arg&&a->vfs)vfs_chdir(a->vfs,arg);}
else if(str_cmp(cmd,"cat")==0){if(arg&&a->vfs){int32_t fd=vfs_open(a->vfs,arg);if(fd>=0){char b[256];int32_t n=vfs_read(a->vfs,fd,b,255);if(n>0){b[n]=0;shell_print(a,b);shell_print(a,"\n");}vfs_close(a->vfs,fd);}}}
else if(a->input_len>0){shell_print(a,"Unknown: ");shell_print(a,cmd);shell_print(a,"\n");}
a->input_len=0;shell_prompt(a);}

void shell_handle_key(shell_app_t*a,char ch){
if(ch=='\n'||ch=='\r'){shell_execute(a);return;}
if(ch=='\b'||ch==127){if(a->input_len>0){a->input_len--;if(a->cursor_col>0)a->cursor_col--;app_put_char((char*)a->screen,a->cursor_row,a->cursor_col,APP_SHELL_LINES,APP_SHELL_COLS,' ');}return;}
if(a->input_len<APP_SHELL_COLS-10){a->input_line[a->input_len++]=ch;app_put_char((char*)a->screen,a->cursor_row,a->cursor_col,APP_SHELL_LINES,APP_SHELL_COLS,ch);a->cursor_col++;
if(a->cursor_col>=APP_SHELL_COLS){a->cursor_col=0;a->cursor_row++;if(a->cursor_row>=APP_SHELL_LINES){app_scroll_up((char*)a->screen,APP_SHELL_LINES,APP_SHELL_COLS);a->cursor_row=APP_SHELL_LINES-1;}}}}

void shell_handle_special(shell_app_t*a,uint8_t sc){
if(sc==0x48&&a->hist_pos>0){a->hist_pos--;while(a->input_len>0){a->input_len--;if(a->cursor_col>0)a->cursor_col--;app_put_char((char*)a->screen,a->cursor_row,a->cursor_col,APP_SHELL_LINES,APP_SHELL_COLS,' ');}
str_copy(a->input_line,a->history[a->hist_pos]);a->input_len=str_len(a->input_line);shell_print(a,a->input_line);}
if(sc==0x50&&a->hist_pos<a->hist_count){a->hist_pos++;while(a->input_len>0){a->input_len--;if(a->cursor_col>0)a->cursor_col--;app_put_char((char*)a->screen,a->cursor_row,a->cursor_col,APP_SHELL_LINES,APP_SHELL_COLS,' ');}
if(a->hist_pos<a->hist_count)str_copy(a->input_line,a->history[a->hist_pos]);else a->input_line[0]=0;a->input_len=str_len(a->input_line);shell_print(a,a->input_line);}}

void shell_render(shell_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];
for(uint32_t r=0;r<APP_SHELL_LINES&&r<20;r++){uint32_t wid=r;
if(wid<GUI_MAX_WIDGETS){win->widgets[wid].type=WIDGET_LABEL;win->widgets[wid].x=5;win->widgets[wid].y=5+r*14;win->widgets[wid].w=win->w-10;win->widgets[wid].h=14;
win->widgets[wid].fg_color=0x00FF00;win->widgets[wid].bg_color=win->bg_color;str_copy(win->widgets[wid].text,a->screen[r]);win->widgets[wid].visible=true;
if(wid>=win->num_widgets)win->num_widgets=wid+1;}}}

/* ---- Editor ---- */
void editor_init(editor_app_t*a,uint32_t gw,vfs_state_t*vfs){
mem_set(a,0,sizeof(*a));a->base.type=APP_EDITOR;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"Editor");
a->vfs=vfs;a->insert_mode=true;a->buffer[0]=0;a->buf_len=0;str_copy(a->filename,"untitled.txt");}

void editor_handle_key(editor_app_t*a,char ch){
if(ch=='\n'||ch=='\r'){if(a->buf_len<APP_EDITOR_MAX_BUF-1){a->buffer[a->buf_len++]='\n';a->cursor_pos=a->buf_len;a->modified=true;}return;}
if(ch=='\b'||ch==127){if(a->cursor_pos>0&&a->buf_len>0){a->cursor_pos--;for(uint32_t i=a->cursor_pos;i<a->buf_len-1;i++)a->buffer[i]=a->buffer[i+1];a->buf_len--;a->modified=true;}return;}
if(a->buf_len<APP_EDITOR_MAX_BUF-1){if(a->insert_mode){for(int32_t i=(int32_t)a->buf_len;i>(int32_t)a->cursor_pos;i--)a->buffer[i]=a->buffer[i-1];a->buf_len++;}else if(a->cursor_pos>=a->buf_len)a->buf_len++;
a->buffer[a->cursor_pos++]=ch;a->modified=true;}}

void editor_handle_special(editor_app_t*a,uint8_t sc){
if(sc==0x4B&&a->cursor_pos>0)a->cursor_pos--;
if(sc==0x4D&&a->cursor_pos<a->buf_len)a->cursor_pos++;
if(sc==0x47){while(a->cursor_pos>0&&a->buffer[a->cursor_pos-1]!='\n')a->cursor_pos--;}
if(sc==0x4F){while(a->cursor_pos<a->buf_len&&a->buffer[a->cursor_pos]!='\n')a->cursor_pos++;}}

int32_t editor_load_file(editor_app_t*a,const char*path){
if(!a->vfs||!path)return -1;int32_t fd=vfs_open(a->vfs,path);if(fd<0)return -1;
int32_t n=vfs_read(a->vfs,fd,a->buffer,APP_EDITOR_MAX_BUF-1);vfs_close(a->vfs,fd);if(n<0)return -1;
a->buffer[n] = 0;
a->buf_len = n;
a->cursor_pos = 0;
a->modified = false;
/* bounded: filename is 64 bytes, a VFS path may be 256 */
uint32_t k = 0;
while (path[k] && k + 1 < sizeof(a->filename)) {
    a->filename[k] = path[k];
    k++;
}
a->filename[k] = 0;
return 0;
}

int32_t editor_save_file(editor_app_t*a){
if(!a->vfs)return -1;int32_t fd=vfs_open(a->vfs,a->filename);if(fd<0)return -1;
int32_t n=vfs_write(a->vfs,fd,a->buffer,a->buf_len);vfs_close(a->vfs,fd);if(n<0)return -1;a->modified=false;return 0;}

void editor_render(editor_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];win->num_widgets=0;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=5;w->w=win->w-10;w->h=14;w->fg_color=0xFFFF00;
str_copy(w->text,a->filename);if(a->modified){int l=str_len(w->text);w->text[l]=' ';w->text[l+1]='*';w->text[l+2]=0;}w->visible=true;}
uint32_t line=0,pos=0;char tmp[128];
while(pos<a->buf_len&&line<24){uint32_t col=0;while(pos<a->buf_len&&a->buffer[pos]!='\n'&&col<120){if(col<127)tmp[col]=a->buffer[pos];col++;pos++;}
tmp[col<127?col:127]=0;if(pos<a->buf_len&&a->buffer[pos]=='\n')pos++;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=20+line*14;w->w=win->w-10;w->h=14;w->fg_color=0xFFFFFF;str_copy(w->text,tmp);w->visible=true;}
line++;}}

/* ---- File Manager ---- */
void fman_init(fman_app_t*a,uint32_t gw,vfs_state_t*vfs){
mem_set(a,0,sizeof(*a));a->base.type=APP_FILE_MANAGER;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"Files");
a->vfs=vfs;if(vfs)str_copy(a->current_path,vfs_get_cwd(vfs));else str_copy(a->current_path,"/");fman_refresh(a);}

void fman_refresh(fman_app_t*a){if(!a->vfs)return;a->num_entries=0;int32_t n=vfs_list_dir(a->vfs,a->current_path,a->entries,APP_FMAN_ROWS);if(n>0)a->num_entries=n;a->selected=0;}
void fman_up(fman_app_t*a){if(a->selected>0)a->selected--;}
void fman_down(fman_app_t*a){if(a->selected<a->num_entries-1)a->selected++;}
void fman_enter(fman_app_t*a){if(a->selected>=a->num_entries)return;vfs_node_t*e=&a->entries[a->selected];if(e->is_dir){vfs_chdir(a->vfs,e->full_path);str_copy(a->current_path,e->full_path);fman_refresh(a);}}
void fman_handle_key(fman_app_t*a,char ch){(void)a;(void)ch;}
void fman_handle_special(fman_app_t*a,uint8_t sc){if(sc==0x48)fman_up(a);if(sc==0x50)fman_down(a);if(sc==0x1C)fman_enter(a);}

void fman_render(fman_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];win->num_widgets=0;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=5;w->w=win->w-10;w->h=14;w->fg_color=0x00FFFF;str_copy(w->text,a->current_path);w->visible=true;}
for(uint32_t i=0;i<a->num_entries&&i<APP_FMAN_ROWS;i++){if(win->num_widgets>=GUI_MAX_WIDGETS)break;gui_widget_t*w=&win->widgets[win->num_widgets++];
w->type=WIDGET_LABEL;w->x=10;w->y=25+i*14;w->w=win->w-20;w->h=14;w->fg_color=(i==a->selected)?0x000000:0xFFFFFF;w->bg_color=(i==a->selected)?0x00AA00:win->bg_color;
int pos=0;if(a->entries[i].is_dir){w->text[pos++]='[';w->text[pos++]='D';w->text[pos++]=']';}else{w->text[pos++]=' ';w->text[pos++]=' ';w->text[pos++]=' ';}
w->text[pos++]=' ';const char*nm=a->entries[i].name;int j=0;while(nm[j]&&pos<120)w->text[pos++]=nm[j++];w->text[pos]=0;w->visible=true;}}

/* ---- System Monitor ---- */
void sysmon_init(sysmon_app_t*a,uint32_t gw,scheduler_t*sched,net_state_t*net,m5_router_t*router,vino_ledger_t*vino,vena_runtime_t*vena){
mem_set(a,0,sizeof(*a));a->base.type=APP_SYSMON;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"SysMon");
a->sched=sched;a->net=net;a->router=router;a->vino=vino;a->vena=vena;}

void sysmon_tick(sysmon_app_t*a){
a->tick++;a->active_tasks=0;
if(a->sched){for(uint32_t i=0;i<a->sched->num_tasks;i++)if(a->sched->tasks[i].state==TASK_RUNNING||a->sched->tasks[i].state==TASK_READY)a->active_tasks++;}
if(a->net){a->net_rx=a->net->rx_packets;a->net_tx=a->net->tx_packets;}
if(a->vino){a->vino_txns=a->vino->num_txns;a->vino_peers=a->vino->num_peers;}
if(a->vena)a->vena_apps=a->vena->active_apps;
a->cpu_usage=(a->tick*7)%100;a->mem_total=262144;a->mem_used=40960+(a->tick%8192);}

void sysmon_handle_key(sysmon_app_t*a,char ch){(void)a;(void)ch;}

static void sysmon_line(gui_window_t*win,uint32_t row,const char*label,uint32_t value){
if(win->num_widgets>=GUI_MAX_WIDGETS)return;gui_widget_t*w=&win->widgets[win->num_widgets++];
w->type=WIDGET_LABEL;w->x=5;w->y=5+row*14;w->w=win->w-10;w->h=14;w->fg_color=0x00FF00;
int pos=0;const char*l=label;while(l[pos]&&pos<60)w->text[pos]=l[pos],pos++;w->text[pos++]=':';w->text[pos++]=' ';
char num[12];num_str(value,num);for(int j=0;num[j]&&pos<120;j++)w->text[pos++]=num[j];w->text[pos]=0;w->visible=true;}

void sysmon_render(sysmon_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];win->num_widgets=0;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=5;w->w=win->w-10;w->h=14;w->fg_color=0xFFFF00;str_copy(w->text,"ZEDEC pqOS System Monitor");w->visible=true;}
sysmon_line(win,1,"Tick",a->tick);sysmon_line(win,2,"CPU(%)",a->cpu_usage);sysmon_line(win,3,"MemTotal(KB)",a->mem_total);
sysmon_line(win,4,"MemUsed(KB)",a->mem_used);sysmon_line(win,5,"Tasks",a->active_tasks);sysmon_line(win,6,"NetRX",a->net_rx);
sysmon_line(win,7,"NetTX",a->net_tx);sysmon_line(win,8,"VinoTxns",a->vino_txns);sysmon_line(win,9,"VinoPeers",a->vino_peers);sysmon_line(win,10,"VenaApps",a->vena_apps);
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_PROGRESS;w->x=5;w->y=175;w->w=win->w-10;w->h=16;w->progress=(int32_t)a->cpu_usage;w->visible=true;}}

/* ---- Network Config ---- */
void netcfg_init(netcfg_app_t*a,uint32_t gw,net_state_t*net,m5_router_t*router){
mem_set(a,0,sizeof(*a));a->base.type=APP_NET_CONFIG;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"NetConfig");
a->net=net;a->router=router;netcfg_refresh(a);}

/* Bounded: 44 adapters with their names need more than detail_buf's 512
 * bytes, and the old loop capped only the name characters, so the separators
 * kept writing past the end of the buffer once it filled. */
void netcfg_refresh(netcfg_app_t *a)
{
    const uint32_t cap = (uint32_t) sizeof(a->detail_buf) - 1;
    uint32_t pos = 0;
    a->detail_len = 0;
    if (!a->router) return;
    const char *hdr = "M5 Omni-Router Adapters:\n";
    for (uint32_t j = 0; hdr[j] && pos < cap; j++) a->detail_buf[pos++] = hdr[j];
    for (uint32_t i = 0; i < M5_PROTO_MAX; i++) {
        if (!a->router->adapters[i].active) continue;
        const char *pn = m5_proto_name((m5_proto_t) i);
        uint32_t n = (uint32_t) str_len(pn);
        if (pos + 4 + n > cap) break; /* " * " + name + newline must fit */
        a->detail_buf[pos++] = ' ';
        a->detail_buf[pos++] = '*';
        a->detail_buf[pos++] = ' ';
        for (uint32_t j = 0; j < n; j++) a->detail_buf[pos++] = pn[j];
        a->detail_buf[pos++] = '\n';
    }
    a->detail_buf[pos] = 0;
    a->detail_len = pos;
}

void netcfg_handle_key(netcfg_app_t*a,char ch){(void)a;(void)ch;}
void netcfg_handle_special(netcfg_app_t*a,uint8_t sc){(void)a;(void)sc;}

void netcfg_render(netcfg_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];win->num_widgets=0;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=5;w->w=win->w-10;w->h=14;w->fg_color=0x00FFFF;str_copy(w->text,"Network Configuration");w->visible=true;}
if(a->net){for(uint32_t i=0;i<a->net->num_interfaces;i++){if(win->num_widgets>=GUI_MAX_WIDGETS)break;gui_widget_t*w=&win->widgets[win->num_widgets++];
w->type=WIDGET_LABEL;w->x=10;w->y=25+i*14;w->w=win->w-20;w->h=14;w->fg_color=0xFFFFFF;int pos=0;const char*nm=a->net->interfaces[i].name;
for(int j=0;nm[j]&&pos<60;j++)w->text[pos++]=nm[j];w->text[pos++]=' ';const char*st=a->net->interfaces[i].up?"[UP]":"[DOWN]";for(int j=0;st[j]&&pos<70;j++)w->text[pos++]=st[j];w->text[pos]=0;w->visible=true;}}
uint32_t row=a->net?a->net->num_interfaces+2:2;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=25+row*14;w->w=win->w-10;w->h=14;w->fg_color=0xFFFF00;str_copy(w->text,"M5 Protocol Adapters:");w->visible=true;row++;}
for(uint32_t i=0;i<M5_PROTO_MAX;i++){if(a->router&&a->router->adapters[i].active){if(win->num_widgets>=GUI_MAX_WIDGETS)break;gui_widget_t*w=&win->widgets[win->num_widgets++];
w->type=WIDGET_LABEL;w->x=10;w->y=25+row*14;w->w=win->w-20;w->h=14;w->fg_color=0x00FF00;const char*pn=m5_proto_name((m5_proto_t)i);int pos=0;w->text[pos++]=' ';w->text[pos++]='*';w->text[pos++]=' ';
for(int j=0;pn[j]&&pos<120;j++)w->text[pos++]=pn[j];w->text[pos]=0;w->visible=true;row++;}}}

/* ---- Wallet ---- */
void wallet_init(wallet_app_t*a,uint32_t gw,vino_ledger_t*vino){
mem_set(a,0,sizeof(*a));a->base.type=APP_WALLET;a->base.gui_win=gw;a->base.active=true;a->base.state=APP_STATE_RUNNING;str_copy(a->base.name,"Wallet");
a->vino=vino;a->current_account[0]=0;a->selected_capital=0;}

void wallet_handle_key(wallet_app_t*a,char ch){(void)a;(void)ch;}
void wallet_handle_special(wallet_app_t*a,uint8_t sc){if(sc==0x48&&a->selected_capital>0)a->selected_capital--;if(sc==0x50&&a->selected_capital<CAP_MAX-1)a->selected_capital++;}

void wallet_render(wallet_app_t*a,gui_desktop_t*gui){
if(a->base.gui_win>=gui->num_windows)return;gui_window_t*win=&gui->windows[a->base.gui_win];win->num_widgets=0;
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=5;w->w=win->w-10;w->h=14;w->fg_color=0xFFFF00;str_copy(w->text,"Vino Wallet - Nine Forms of Capital");w->visible=true;}
if(a->current_account[0]==0&&a->vino&&a->vino->num_accounts>0)str_copy(a->current_account,a->vino->balances[0].address);
if(win->num_widgets<GUI_MAX_WIDGETS){gui_widget_t*w=&win->widgets[win->num_widgets++];w->type=WIDGET_LABEL;w->x=5;w->y=25;w->w=win->w-10;w->h=14;w->fg_color=0x00FFFF;
int pos=0;const char*hd="Account: ";while(hd[pos])w->text[pos]=hd[pos],pos++;const char*ac=a->current_account;for(int j=0;ac[j]&&pos<120;j++)w->text[pos++]=ac[j];w->text[pos]=0;w->visible=true;}
vino_account_t*acc=0;if(a->vino&&a->current_account[0])acc=vino_get_account(a->vino,a->current_account);
for(uint32_t i=0;i<CAP_MAX;i++){if(win->num_widgets>=GUI_MAX_WIDGETS)break;gui_widget_t*w=&win->widgets[win->num_widgets++];
w->type=WIDGET_LABEL;w->x=10;w->y=50+i*16;w->w=win->w-20;w->h=14;w->fg_color=(i==a->selected_capital)?0x000000:0xFFFFFF;w->bg_color=(i==a->selected_capital)?0x00AA00:win->bg_color;
const char*cn=vino_capital_name((capital_type_t)i);int pos=0;for(int j=0;cn[j]&&pos<30;j++)w->text[pos++]=cn[j];while(pos<32)w->text[pos++]=' ';
uint64_t bal=acc?acc->balance[i]:0;char num[20];int ni=0;if(bal==0)num[ni++]='0';while(bal>0){num[ni++]='0'+(bal%10);bal/=10;}
for(int j=ni-1;j>=0&&pos<120;j--)w->text[pos++]=num[j];w->text[pos]=0;w->visible=true;}}

/* ---- App Launcher ---- */
void app_launcher_init(app_launcher_t*al,gui_desktop_t*gui,vfs_state_t*vfs,net_state_t*net,m5_router_t*router,vino_ledger_t*vino,vena_runtime_t*vena,scheduler_t*sched){
mem_set(al,0,sizeof(*al));al->gui=gui;al->vfs=vfs;al->net=net;al->router=router;al->vino=vino;al->vena=vena;al->sched=sched;al->num_windows=0;al->active_app=0xFFFFFFFF;}

int32_t app_launcher_open(app_launcher_t*al,app_type_t type){
uint32_t gw=al->num_windows;
if(gw>=GUI_MAX_WINDOWS)return -1;
/* Create GUI window */
int32_t x=50+(gw%4)*30,y=50+(gw%4)*20,w=400,h=350;
uint32_t colors[6]={0x001100,0x000011,0x110000,0x001111,0x111100,0x000022};
gui_create_window(al->gui,al->gui->windows[gw].title,x,y,w,h,colors[gw%6]);

switch(type){
case APP_SHELL:shell_init(&al->shell,gw,al->vfs,al->net,al->router,al->vino,al->vena,al->sched);break;
case APP_EDITOR:editor_init(&al->editor,gw,al->vfs);break;
case APP_FILE_MANAGER:fman_init(&al->fman,gw,al->vfs);break;
case APP_SYSMON:sysmon_init(&al->sysmon,gw,al->sched,al->net,al->router,al->vino,al->vena);break;
case APP_NET_CONFIG:netcfg_init(&al->netcfg,gw,al->net,al->router);break;
case APP_WALLET:wallet_init(&al->wallet,gw,al->vino);break;
case APP_EXCHANGE:derivatives_init(&al->derivatives,gw,al->vino,al->vena);break;
case APP_BANK:assurance_init(&al->assurance,gw,al->vino,al->vena);break;
case APP_CUSTOM:treaty_init(&al->treaty,gw,al->vino,al->vena);break;
default:return -1;}
al->num_windows++;al->active_app=type;return 0;}

int32_t app_launcher_close(app_launcher_t*al,app_type_t type){
switch(type){
case APP_SHELL:al->shell.base.active=false;break;
case APP_EDITOR:al->editor.base.active=false;break;
case APP_FILE_MANAGER:al->fman.base.active=false;break;
case APP_SYSMON:al->sysmon.base.active=false;break;
case APP_NET_CONFIG:al->netcfg.base.active=false;break;
case APP_WALLET:al->wallet.base.active=false;break;
case APP_EXCHANGE:al->derivatives.base.active=false;break;
case APP_BANK:al->assurance.base.active=false;break;
case APP_CUSTOM:al->treaty.base.active=false;break;
default:return -1;}
if(al->active_app==type)al->active_app=0xFFFFFFFF;return 0;}

void app_launcher_handle_key(app_launcher_t*al,char ch,uint8_t sc){
switch(al->active_app){
case APP_SHELL:if(ch)shell_handle_key(&al->shell,ch);if(sc)shell_handle_special(&al->shell,sc);break;
case APP_EDITOR:if(ch)editor_handle_key(&al->editor,ch);if(sc)editor_handle_special(&al->editor,sc);break;
case APP_FILE_MANAGER:if(sc)fman_handle_special(&al->fman,sc);break;
case APP_SYSMON:if(ch)sysmon_handle_key(&al->sysmon,ch);break;
case APP_NET_CONFIG:if(sc)netcfg_handle_special(&al->netcfg,sc);break;
case APP_WALLET:if(sc)wallet_handle_special(&al->wallet,sc);break;
case APP_EXCHANGE:if(ch)derivatives_handle_key(&al->derivatives,ch);if(sc)derivatives_handle_special(&al->derivatives,sc);break;
case APP_BANK:if(ch)assurance_handle_key(&al->assurance,ch);if(sc)assurance_handle_special(&al->assurance,sc);break;
case APP_CUSTOM:if(ch)treaty_handle_key(&al->treaty,ch);if(sc)treaty_handle_special(&al->treaty,sc);break;
default:break;}}

void app_launcher_render(app_launcher_t*al){
if(al->shell.base.active)shell_render(&al->shell,al->gui);
if(al->editor.base.active)editor_render(&al->editor,al->gui);
if(al->fman.base.active)fman_render(&al->fman,al->gui);
if(al->sysmon.base.active)sysmon_render(&al->sysmon,al->gui);
if(al->netcfg.base.active)netcfg_render(&al->netcfg,al->gui);
if(al->wallet.base.active)wallet_render(&al->wallet,al->gui);
if(al->derivatives.base.active)derivatives_render(&al->derivatives,al->gui);
if(al->assurance.base.active)assurance_render(&al->assurance,al->gui);
if(al->treaty.base.active)treaty_render(&al->treaty,al->gui);}

void app_launcher_tick(app_launcher_t*al){
if(al->sysmon.base.active)sysmon_tick(&al->sysmon);
if(al->netcfg.base.active)netcfg_refresh(&al->netcfg);
if(al->fman.base.active)fman_refresh(&al->fman);
if(al->derivatives.base.active)derivatives_tick(&al->derivatives);
if(al->assurance.base.active)assurance_tick(&al->assurance);
if(al->treaty.base.active)treaty_tick(&al->treaty);}

/* ---- NOT DECLARED, AND WHY -------------------------------------------------
 * Every other module wired into this build in this pass carries a ZXV_DECLARE.
 * This one deliberately does not, and the reason is written here rather than
 * only in a report, because an absent declaration is exactly the silence the
 * mechanism exists to remove -- so the absence has to be louder than the
 * declaration would have been.
 *
 * THE HONEST DECLARATION FOR THIS FILE IS:
 *     ZXV_DECLARE(apps, ZXV_PROVIDES(apps_ready),
 *                       ZXV_REQUIRES(vfs_ready, gui_ready), ...)
 * measured from apps.o's `nm -u`: vfs_chdir/close/get_cwd/list_dir/open/read/
 * write -> vfs_ready (provided, by kernel/src/vfs/vfs.c), and gui_create_window
 * -> a GUI backend that NOTHING IN THIS IMAGE PROVIDES. Every other undefined
 * symbol in apps.o (vino_*, vena_*, m5_*) resolves; that one does not. It links
 * today only because --gc-sections discards the referring section before
 * relocation.
 *
 * WHY THE DECLARATION IS NOT WRITTEN ANYWAY. A REQUIRES naming a capability no
 * module PROVIDES is MB_ERR_UNPROVIDED, and modbind_verify_graph does not fail
 * that module -- it fails THE GRAPH. The arch main then prints "bring-up
 * REFUSED: fix the graph, not the symptom" and runs NONE of the 87 bring-ups.
 * One unbuildable GUI edge would switch off every other module's boot-time
 * self-check, which is a strictly worse outcome than a declaration that is
 * missing and says so.
 *
 * WHAT WOULD FIX IT, precisely: add a GUI backend to KERNEL_SRCS so something
 * defines gui_create_window (05_KERNEL/gui/gui.c compiles clean under these
 * arm64 CFLAGS -- agent A measured that), give it a declaration PROVIDING
 * gui_ready, and then add the ZXV_DECLARE above to this file. That is a
 * Makefile change, which this pass does not own.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV composition slice)
 * SPDX-License-Identifier: Apache-2.0
 */
