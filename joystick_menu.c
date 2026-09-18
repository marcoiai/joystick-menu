#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glob.h>

static MIX_Mixer *mixer = NULL;
static MIX_Audio *music = NULL;
static MIX_Track *music_track = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *logo_texture = NULL;
static SDL_Texture *background_texture = NULL;
static SDL_Texture *cover_texture = NULL;
static TTF_Font *font = NULL;

#define INPUT_COOLDOWN_MS 200
#define AXIS_DEADZONE 8000
#define LOGO_HEIGHT 200
#define FONT_SIZE 18
#define MAX_INPUT_LENGTH 256

static Uint64 last_input_time = 0;
static char input_text[MAX_INPUT_LENGTH] = "";
static int typing_in_input = 0;

typedef struct { const char *dir_name; const char *display_name; const char *mame_sys; const char *launch_arg; const char *allowed_exts; } SystemEntry;
static const SystemEntry systems[] = {
    { "sms1", "Master System", "sms1", "-cart", "sms,bin,zip" },
    { "genesis", "Mega Drive", "genesis", "-cart", "md,bin,zip" },
    { "snes", "Super Nintendo", "snes", "-cart", "smc,sfc,zip" },
    { "nes", "Nintendo 8-bit", "nes", "-cart", "nes,zip" },
    { "segacd", "Mega CD", "segacd", "-cdrom", "cue,chd,iso" },
    { "psu", "PlayStation 1", "psu", "-cdrom", "cue,chd,iso" },
    { "neogeo", "Neo Geo", "neogeo", NULL, "neo" },
    { "ps2", "PlayStation 2", "pcsx2", NULL, "iso,chd,cso" },
    { "ps3", "PlayStation 3", "rpcs3", NULL, "iso,pkg" },
};

static int selected_system_index = 0, system_scroll_offset = 0, in_rom_menu = 0;
static int system_menu_count = sizeof(systems) / sizeof(SystemEntry) + 2;
typedef struct { char *display_name; char *rom_path; } RomEntry;
static RomEntry *all_rom_list = NULL, *rom_list = NULL;
static int all_rom_count = 0, rom_count = 0, selected_rom_index = 0, rom_scroll_offset = 0;

static void draw_system_menu(void); static void draw_rom_menu(void); static void draw_search_field(void);
static void load_rom_list(const SystemEntry *sys); static void free_rom_list(void); static void free_all_rom_list(void); static void filter_rom_list(void);
static void handle_events(const SDL_Event *event); static void handle_joystick_input(const SDL_Event *event);
static void move_selection(int direction); static void activate_selection(void); static void leave_rom_menu(void);
static int has_allowed_extension(const char *filename, const char *allowed_exts);
static void render_text_centered(const char *text, float y, SDL_Color color); static void render_text(const char *text, float x, float y, SDL_Color color);
static void draw_scrollbar(int item_count, int visible_lines, int scroll_offset, int start_y, int line_height, int win_w);
static int file_exists(const char *path); static SDL_Texture *load_cover_for_rom(const char *rom_path); static int launch_pcsx2(const char *rom_path); static int launch_rpcs3(const char *rom_path);

static void draw_system_menu(void) {
    int win_w, win_h; SDL_GetWindowSize(window, &win_w, &win_h); int line_height=FONT_SIZE+10, visible=(win_h-LOGO_HEIGHT-40)/line_height;
    if(selected_system_index<system_scroll_offset) system_scroll_offset=selected_system_index;
    if(selected_system_index>=system_scroll_offset+visible) system_scroll_offset=selected_system_index-visible+1;
    int start_y=LOGO_HEIGHT+20;
    for(int i=system_scroll_offset;i<system_menu_count && i<system_scroll_offset+visible;i++) {
        SDL_Color c={200,200,200,255}; if(i==selected_system_index)c.r=c.g=255;
        const char *label=i<system_menu_count-2?systems[i].display_name:(i==system_menu_count-2?"Run Cover Scraper":"Exit");
        render_text_centered(label,start_y+(i-system_scroll_offset)*line_height,c);
    }
    draw_scrollbar(system_menu_count,visible,system_scroll_offset,start_y,line_height,win_w);
}

static void draw_search_field(void) {
    int w,h; SDL_GetWindowSize(window,&w,&h); float y=h-FONT_SIZE-42;
    SDL_FRect r={50.0f,y,w-100.0f,FONT_SIZE+10.0f};
    SDL_SetRenderDrawColor(renderer,50,50,50,210); SDL_RenderFillRect(renderer,&r);
    if(typing_in_input) SDL_SetRenderDrawColor(renderer,255,255,0,255); else SDL_SetRenderDrawColor(renderer,110,110,110,255);
    SDL_RenderRect(renderer,&r);
    char text[MAX_INPUT_LENGTH+32]; snprintf(text,sizeof(text),"Search [TAB]: %s%s",input_text,typing_in_input&&((SDL_GetTicks()/500)%2)?"|":"");
    render_text(text,56.0f,y+5.0f,(SDL_Color){255,255,255,255});
}

static void draw_rom_menu(void) {
    int win_w,win_h; SDL_GetWindowSize(window,&win_w,&win_h); int line_height=FONT_SIZE+10, visible=(win_h-LOGO_HEIGHT-80)/line_height;
    if(selected_rom_index<rom_scroll_offset)rom_scroll_offset=selected_rom_index;
    if(selected_rom_index>=rom_scroll_offset+visible)rom_scroll_offset=selected_rom_index-visible+1;
    int start_y=LOGO_HEIGHT+20;
    for(int i=rom_scroll_offset;i<rom_count&&i<rom_scroll_offset+visible;i++) { SDL_Color c={200,200,200,255}; if(i==selected_rom_index)c.r=c.g=255; render_text_centered(rom_list[i].display_name,start_y+(i-rom_scroll_offset)*line_height,c); }
    draw_scrollbar(rom_count,visible,rom_scroll_offset,start_y,line_height,win_w);
    if(rom_list&&rom_count>0&&selected_rom_index<rom_count&&rom_list[selected_rom_index].rom_path) {
        if(cover_texture){SDL_DestroyTexture(cover_texture);cover_texture=NULL;} cover_texture=load_cover_for_rom(rom_list[selected_rom_index].rom_path);
        if(!cover_texture)cover_texture=IMG_LoadTexture(renderer,"assets/cover.png");
        if(cover_texture){SDL_FRect dst={win_w-230.0f,30.0f,220.0f,220.0f};SDL_RenderTexture(renderer,cover_texture,NULL,&dst);}
    }
    draw_search_field();
}

int main(int argc,char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_AUDIO); TTF_Init(); SDL_CreateWindowAndRenderer("Joystick Menu",1024,768,0,&window,&renderer);
    font=TTF_OpenFont("assets/Roboto-Regular.ttf",FONT_SIZE); logo_texture=IMG_LoadTexture(renderer,"assets/logo.png"); background_texture=IMG_LoadTexture(renderer,"assets/background.jpg");
    if(background_texture){SDL_SetTextureBlendMode(background_texture,SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaMod(background_texture,80);}
    if(MIX_Init()) { mixer=MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,NULL); if(mixer){ music=MIX_LoadAudio(mixer,"assets/background1.ogg",false); if(music){ music_track=MIX_CreateTrack(mixer); if(music_track&&MIX_SetTrackAudio(music_track,music)){MIX_SetTrackGain(music_track,0.5f);SDL_PropertiesID p=SDL_CreateProperties();SDL_SetNumberProperty(p,MIX_PROP_PLAY_LOOPS_NUMBER,-1);MIX_PlayTrack(music_track,p);SDL_DestroyProperties(p);}}}}
    SDL_Event event; int running=1;
    while(running){while(SDL_PollEvent(&event)){if(event.type==SDL_EVENT_QUIT)running=0;if(event.type==SDL_EVENT_JOYSTICK_ADDED)SDL_OpenJoystick(event.jdevice.which);if(event.type==SDL_EVENT_JOYSTICK_REMOVED)SDL_CloseJoystick(SDL_GetJoystickFromID(event.jdevice.which));handle_events(&event);handle_joystick_input(&event);}int w,h;SDL_GetWindowSize(window,&w,&h);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);if(background_texture){SDL_FRect d={0,0,(float)w,(float)h};SDL_RenderTexture(renderer,background_texture,NULL,&d);}if(logo_texture){SDL_FRect d={(w-200)/2.0f,40,200,100};SDL_RenderTexture(renderer,logo_texture,NULL,&d);}if(in_rom_menu)draw_rom_menu();else draw_system_menu();render_text("by MARCO AURELIO SIMAO",10,h-FONT_SIZE-10,(SDL_Color){150,150,150,255});SDL_RenderPresent(renderer);SDL_Delay(16);}
    free_rom_list();free_all_rom_list();TTF_CloseFont(font);SDL_DestroyTexture(logo_texture);SDL_DestroyTexture(background_texture);if(cover_texture)SDL_DestroyTexture(cover_texture);if(music_track)MIX_DestroyTrack(music_track);if(music)MIX_DestroyAudio(music);if(mixer)MIX_DestroyMixer(mixer);MIX_Quit();TTF_Quit();SDL_Quit();return 0;
}

static void render_text_centered(const char *text,float y,SDL_Color color){SDL_Surface*s=TTF_RenderText_Blended(font,text,SDL_strlen(text),color);if(!s)return;SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);int tw=s->w,th=s->h;SDL_DestroySurface(s);if(!t)return;int w;SDL_GetWindowSize(window,&w,NULL);SDL_FRect d={(w-tw)/2.0f,y,(float)tw,(float)th};SDL_RenderTexture(renderer,t,NULL,&d);SDL_DestroyTexture(t);}
static void render_text(const char *text,float x,float y,SDL_Color color){SDL_Surface*s=TTF_RenderText_Blended(font,text,SDL_strlen(text),color);if(!s)return;SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);int tw=s->w,th=s->h;SDL_DestroySurface(s);if(!t)return;SDL_FRect d={x,y,(float)tw,(float)th};SDL_RenderTexture(renderer,t,NULL,&d);SDL_DestroyTexture(t);}
static void draw_scrollbar(int n,int visible,int off,int y,int lh,int w){if(n<=visible||visible<=0)return;float sh=visible*lh,hh=sh*(visible/(float)n);if(hh<10)hh=10;float hy=y+(off/(float)(n-visible))*(sh-hh);SDL_FRect b={w-20.0f,(float)y,8,sh},h={w-20.0f,hy,8,hh};SDL_SetRenderDrawColor(renderer,80,80,80,200);SDL_RenderFillRect(renderer,&b);SDL_SetRenderDrawColor(renderer,200,200,200,255);SDL_RenderFillRect(renderer,&h);}
static int has_allowed_extension(const char*f,const char*a){const char*d=strrchr(f,'.');if(!d||d==f)return 0;char ext[16],tmp[64];SDL_strlcpy(ext,d+1,sizeof(ext));SDL_strlcpy(tmp,a,sizeof(tmp));for(char*t=strtok(tmp,",");t;t=strtok(NULL,","))if(SDL_strcasecmp(ext,t)==0)return 1;return 0;}

static void load_rom_list(const SystemEntry *sys){free_rom_list();free_all_rom_list();char path[512];snprintf(path,sizeof(path),"./roms/%s/",sys->dir_name);DIR*dir=opendir(path);if(!dir)return;int cap=20;all_rom_list=calloc(cap,sizeof(RomEntry));struct dirent*e;
    while((e=readdir(dir))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char fp[1024];snprintf(fp,sizeof(fp),"./roms/%s/%s",sys->dir_name,e->d_name);struct stat st;if(stat(fp,&st))continue;if(S_ISREG(st.st_mode)&&has_allowed_extension(e->d_name,sys->allowed_exts)){if(all_rom_count>=cap){cap*=2;all_rom_list=realloc(all_rom_list,cap*sizeof(RomEntry));}all_rom_list[all_rom_count].display_name=strdup(e->d_name);all_rom_list[all_rom_count++].rom_path=strdup(fp);}}
    rewinddir(dir);while((e=readdir(dir))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char sp[1024];snprintf(sp,sizeof(sp),"./roms/%s/%s",sys->dir_name,e->d_name);struct stat st;if(stat(sp,&st)||!S_ISDIR(st.st_mode))continue;DIR*sd=opendir(sp);if(!sd)continue;struct dirent*se;while((se=readdir(sd)))if(se->d_type==DT_REG&&has_allowed_extension(se->d_name,sys->allowed_exts)){if(all_rom_count>=cap){cap*=2;all_rom_list=realloc(all_rom_list,cap*sizeof(RomEntry));}char fp[1024];snprintf(fp,sizeof(fp),"%s/%s",sp,se->d_name);all_rom_list[all_rom_count].display_name=strdup(se->d_name);all_rom_list[all_rom_count++].rom_path=strdup(fp);}closedir(sd);}closedir(dir);filter_rom_list();}
static void free_all_rom_list(void){if(all_rom_list){for(int i=0;i<all_rom_count;i++){SDL_free(all_rom_list[i].display_name);SDL_free(all_rom_list[i].rom_path);}SDL_free(all_rom_list);}all_rom_list=NULL;all_rom_count=0;}
static void free_rom_list(void){SDL_free(rom_list);rom_list=NULL;rom_count=0;if(cover_texture){SDL_DestroyTexture(cover_texture);cover_texture=NULL;}}
static void filter_rom_list(void){free_rom_list();rom_list=calloc(all_rom_count+1,sizeof(RomEntry));if(!rom_list)return;for(int i=0;i<all_rom_count;i++)if(!input_text[0]||SDL_strcasestr(all_rom_list[i].display_name,input_text)){rom_list[rom_count++]=all_rom_list[i];}rom_list[rom_count].display_name="Exit";rom_list[rom_count].rom_path=NULL;rom_count++;selected_rom_index=0;rom_scroll_offset=0;}
static void leave_rom_menu(void){typing_in_input=0;SDL_StopTextInput(window);input_text[0]='\0';in_rom_menu=0;free_rom_list();free_all_rom_list();}
static void move_selection(int d){if(typing_in_input)return;if(in_rom_menu){if(rom_count>0)selected_rom_index=(selected_rom_index+rom_count+d)%rom_count;}else selected_system_index=(selected_system_index+system_menu_count+d)%system_menu_count;}

static int launch_pcsx2(const char *rom_path){
    const char *names[] = { "pcsx2-qt", "pcsx2", "PCSX2", "PCSX2-Qt", "PCSX2-qt", NULL };
    char cmd[8192];

    for(int i=0; names[i]; i++){
        snprintf(cmd,sizeof(cmd),"command -v %s >/dev/null 2>&1",names[i]);
        if(system(cmd)==0){
            snprintf(cmd,sizeof(cmd),"%s -batch -fastboot -fullscreen -- \"%s\"",names[i],rom_path);
            return system(cmd)==0;
        }
    }

    const char *patterns[] = {
        "./pcsx2*.AppImage",
        "./PCSX2*.AppImage",
        NULL
    };

    for(int i=0; patterns[i]; i++){
        glob_t g; memset(&g,0,sizeof(g));
        if(glob(patterns[i],0,NULL,&g)==0 && g.gl_pathc>0){
            const char *bin=g.gl_pathv[0];
            if(access(bin,X_OK)!=0) chmod(bin,0755);
            snprintf(cmd,sizeof(cmd),"\"%s\" -batch -fastboot -fullscreen -- \"%s\"",bin,rom_path);
            int ok=system(cmd)==0;
            globfree(&g);
            return ok;
        }
        globfree(&g);
    }

    SDL_Log("PCSX2 not found. Downloading official stable Linux AppImage...");

    const char *url_cmd =
        "curl -fsSL https://api.github.com/repos/PCSX2/pcsx2/releases/latest "
        "| grep -o 'https://[^\"]*linux-appimage-x64-Qt.AppImage' "
        "| head -n1";

    FILE *fp = popen(url_cmd,"r");
    if(!fp){
        SDL_Log("Could not query PCSX2 release URL.");
        return 0;
    }

    char url[4096] = "";
    if(fgets(url,sizeof(url),fp)){
        size_t n=strlen(url);
        while(n>0 && (url[n-1]=='\n' || url[n-1]=='\r')) url[--n]='\0';
    }
    pclose(fp);

    if(!url[0]){
        SDL_Log("Could not find an official PCSX2 Linux AppImage asset.");
        return 0;
    }

    pid_t download_pid = fork();
    if(download_pid == 0){
        execlp("curl","curl","-fL","--progress-bar",url,"-o","./PCSX2.AppImage",(char*)NULL);
        _exit(127);
    }
    if(download_pid < 0){
        SDL_Log("Could not start PCSX2 download.");
        return 0;
    }

    int download_status = 0;
    int done = 0;
    while(!done){
        pid_t r = waitpid(download_pid,&download_status,WNOHANG);
        if(r == download_pid){
            done = 1;
            break;
        }
        if(r < 0){
            SDL_Log("Error while waiting for PCSX2 download.");
            return 0;
        }

        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            if(ev.type == SDL_EVENT_QUIT){
                SDL_Log("Download continues until process exits.");
            }
        }

        int w,h;
        SDL_GetWindowSize(window,&w,&h);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        char loading[64];
        int dots = (int)((SDL_GetTicks()/400)%4);
        snprintf(loading,sizeof(loading),"Downloading PCSX2%.*s",dots,"...");

        render_text_centered(loading,(float)h/2.0f-20.0f,(SDL_Color){255,255,255,255});
        render_text_centered("First run only",(float)h/2.0f+15.0f,(SDL_Color){160,160,160,255});
        SDL_RenderPresent(renderer);
        SDL_Delay(50);
    }

    if(!WIFEXITED(download_status) || WEXITSTATUS(download_status)!=0){
        SDL_Log("PCSX2 download failed.");
        return 0;
    }

    if(chmod("./PCSX2.AppImage",0755)!=0){
        SDL_Log("Could not make PCSX2.AppImage executable.");
        return 0;
    }

    snprintf(cmd,sizeof(cmd),
        "\"./PCSX2.AppImage\" -batch -fastboot -fullscreen -- \"%s\"",
        rom_path);

    return system(cmd)==0;
}

static int launch_rpcs3(const char *rom_path){
    const char *names[] = { "rpcs3", "RPCS3", NULL };
    char cmd[8192];

    for(int i=0; names[i]; i++){
        snprintf(cmd,sizeof(cmd),"command -v %s >/dev/null 2>&1",names[i]);
        if(system(cmd)==0){
            snprintf(cmd,sizeof(cmd),"%s --no-gui \"%s\"",names[i],rom_path);
            return system(cmd)==0;
        }
    }

    const char *patterns[] = {
        "./rpcs3*.AppImage",
        "./RPCS3*.AppImage",
        NULL
    };

    for(int i=0; patterns[i]; i++){
        glob_t g; memset(&g,0,sizeof(g));
        if(glob(patterns[i],0,NULL,&g)==0 && g.gl_pathc>0){
            const char *bin=g.gl_pathv[0];
            if(access(bin,X_OK)!=0) chmod(bin,0755);
            snprintf(cmd,sizeof(cmd),"\"%s\" --no-gui \"%s\"",bin,rom_path);
            int ok=system(cmd)==0;
            globfree(&g);
            return ok;
        }
        globfree(&g);
    }

    SDL_Log("RPCS3 not found. Downloading latest Linux AppImage...");

    const char *url_cmd =
        "curl -fsSL https://api.github.com/repos/RPCS3/rpcs3-binaries-linux/releases/latest "
        "| grep -o 'https://[^\"]*linux64.AppImage' "
        "| head -n1";

    FILE *fp = popen(url_cmd,"r");
    if(!fp){
        SDL_Log("Could not query RPCS3 release URL.");
        return 0;
    }

    char url[4096] = "";
    if(fgets(url,sizeof(url),fp)){
        size_t n=strlen(url);
        while(n>0 && (url[n-1]=='\n' || url[n-1]=='\r')) url[--n]='\0';
    }
    pclose(fp);

    if(!url[0]){
        SDL_Log("Could not find an RPCS3 Linux AppImage asset.");
        return 0;
    }

    pid_t download_pid = fork();
    if(download_pid == 0){
        execlp("curl","curl","-fL","--progress-bar",url,"-o","./RPCS3.AppImage",(char*)NULL);
        _exit(127);
    }
    if(download_pid < 0){
        SDL_Log("Could not start RPCS3 download.");
        return 0;
    }

    int download_status = 0;
    while(1){
        pid_t r = waitpid(download_pid,&download_status,WNOHANG);
        if(r == download_pid) break;
        if(r < 0){
            SDL_Log("Error while waiting for RPCS3 download.");
            return 0;
        }

        SDL_Event ev;
        while(SDL_PollEvent(&ev)){ }

        int w,h;
        SDL_GetWindowSize(window,&w,&h);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        char loading[64];
        int dots = (int)((SDL_GetTicks()/400)%4);
        snprintf(loading,sizeof(loading),"Downloading RPCS3%.*s",dots,"...");

        render_text_centered(loading,(float)h/2.0f-20.0f,(SDL_Color){255,255,255,255});
        render_text_centered("First run only",(float)h/2.0f+15.0f,(SDL_Color){160,160,160,255});
        SDL_RenderPresent(renderer);
        SDL_Delay(50);
    }

    if(!WIFEXITED(download_status) || WEXITSTATUS(download_status)!=0){
        SDL_Log("RPCS3 download failed.");
        return 0;
    }

    if(chmod("./RPCS3.AppImage",0755)!=0){
        SDL_Log("Could not make RPCS3.AppImage executable.");
        return 0;
    }

    snprintf(cmd,sizeof(cmd),"\"./RPCS3.AppImage\" --no-gui \"%s\"",rom_path);
    return system(cmd)==0;
}


static void activate_selection(void){if(typing_in_input)return;if(in_rom_menu){if(!rom_list||rom_count<=0)return;if(!rom_list[selected_rom_index].rom_path){leave_rom_menu();return;}const SystemEntry*sys=&systems[selected_system_index];const char*rp=rom_list[selected_rom_index].rom_path;char final[1024]="";struct stat st;if(stat(rp,&st))return;if(S_ISREG(st.st_mode))snprintf(final,sizeof(final),"%s",rp);if(!final[0])return;if(music_track)MIX_PauseTrack(music_track);char cmd[2048];if(!strcmp(sys->mame_sys,"neogeo")){const char*s=strrchr(final,'/');s=s?s+1:final;const char*d=strrchr(s,'.');char id[256];size_t n=d?(size_t)(d-s):strlen(s);if(n>=sizeof(id))n=sizeof(id)-1;memcpy(id,s,n);id[n]='\0';snprintf(cmd,sizeof(cmd),"mame %s %s",sys->mame_sys,id);}else if(!strcmp(sys->mame_sys,"pcsx2")){launch_pcsx2(final);}else if(!strcmp(sys->mame_sys,"rpcs3")){launch_rpcs3(final);}else{snprintf(cmd,sizeof(cmd),"mame %s %s \"%s\"",sys->mame_sys,sys->launch_arg,final);system(cmd);}if(music_track)MIX_ResumeTrack(music_track);leave_rom_menu();return;}if(selected_system_index==system_menu_count-1)exit(0);if(selected_system_index==system_menu_count-2){pid_t p=fork();if(p==0){execl("./cover-scraper","./cover-scraper",(char*)NULL);_exit(1);}if(p>0){int s;waitpid(p,&s,0);}return;}input_text[0]='\0';load_rom_list(&systems[selected_system_index]);in_rom_menu=1;}

static void handle_events(const SDL_Event*e){if(e->type==SDL_EVENT_TEXT_INPUT&&typing_in_input){size_t left=MAX_INPUT_LENGTH-1-strlen(input_text);if(left){strncat(input_text,e->text.text,left);filter_rom_list();}return;}if(e->type!=SDL_EVENT_KEY_DOWN||e->key.repeat)return;
    if(in_rom_menu&&e->key.key==SDLK_TAB){typing_in_input=!typing_in_input;if(typing_in_input)SDL_StartTextInput(window);else SDL_StopTextInput(window);return;}
    if(typing_in_input){if(e->key.key==SDLK_BACKSPACE&&input_text[0]){size_t n=strlen(input_text);input_text[n-1]='\0';filter_rom_list();}else if(e->key.key==SDLK_RETURN||e->key.key==SDLK_KP_ENTER){typing_in_input=0;SDL_StopTextInput(window);}else if(e->key.key==SDLK_ESCAPE){typing_in_input=0;SDL_StopTextInput(window);input_text[0]='\0';filter_rom_list();}return;}
    switch(e->key.key){case SDLK_UP:move_selection(-1);break;case SDLK_DOWN:move_selection(1);break;case SDLK_RETURN:case SDLK_KP_ENTER:case SDLK_SPACE:activate_selection();break;case SDLK_ESCAPE:if(in_rom_menu)leave_rom_menu();break;default:break;}}
static void handle_joystick_input(const SDL_Event*e){if(typing_in_input)return;Uint64 now=SDL_GetTicks();if(now<last_input_time+INPUT_COOLDOWN_MS)return;if(e->type==SDL_EVENT_JOYSTICK_AXIS_MOTION&&e->jaxis.axis==1){int d=e->jaxis.value<-AXIS_DEADZONE?-1:(e->jaxis.value>AXIS_DEADZONE?1:0);if(d){move_selection(d);last_input_time=now;}}if(e->type==SDL_EVENT_JOYSTICK_BUTTON_DOWN&&e->jbutton.button==0){activate_selection();last_input_time=now;}}
static int file_exists(const char*p){struct stat st;return stat(p,&st)==0;}
static SDL_Texture*load_cover_for_rom(const char*rp){if(!rp)return NULL;const char*f=strrchr(rp,'/');f=f?f+1:rp;const char*d=strrchr(f,'.');int n=d?(int)(d-f):(int)strlen(f);char p[512];snprintf(p,sizeof(p),"./covers/%.*s.png",n,f);if(file_exists(p)){SDL_Texture*t=IMG_LoadTexture(renderer,p);if(t)return t;}snprintf(p,sizeof(p),"./covers/%.*s.jpg",n,f);return file_exists(p)?IMG_LoadTexture(renderer,p):NULL;}
