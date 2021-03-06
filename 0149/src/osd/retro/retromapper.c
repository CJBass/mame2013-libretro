void retro_poll_mame_input();

static int rtwi=320,rthe=240,topw=1024; // DEFAULT TEXW/TEXH/PITCH
int SHIFTON=-1;
char RPATH[512];

extern "C" int mmain(int argc, const char *argv);
extern bool draw_this_frame;

#define M16B

#ifdef M16B
	uint16_t videoBuffer[1024*1024];
	#define PITCH 1
#else
	unsigned int videoBuffer[1024*1024];
	#define PITCH 2*1
#endif 

retro_video_refresh_t video_cb = NULL;
retro_environment_t environ_cb = NULL;

static retro_input_state_t input_state_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
static retro_input_poll_t input_poll_cb;

void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { }
void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{   	
   memset(info, 0, sizeof(*info));
   info->library_name = "MAME 2013";
   info->library_version = "0.149";
   info->valid_extensions = "zip|chd";
   info->need_fullpath = true;   
   info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width = 640;
   info->geometry.base_height =480;

#ifndef RETRO_AND 
   info->geometry.max_width = 1024;
   info->geometry.max_height = 768;
#else    	
   info->geometry.max_width =  640;
   info->geometry.max_height = 480;
#endif
   info->geometry.aspect_ratio =4/3;
   info->timing.fps = 60;
   info->timing.sample_rate = 48000.0;
}

static void retro_wrap_emulator()
{    
    mmain(1,RPATH);

    pauseg=-1;

    environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0); 

    // Were done here
    co_switch(mainThread);
        
    // Dead emulator, but libco says not to return
    while(true)
    {
        LOGI("Running a dead emulator.");
        co_switch(mainThread);
    }
}

void retro_init (void){ 

#ifndef M16B
    	enum retro_pixel_format fmt =RETRO_PIXEL_FORMAT_XRGB8888;
#else
    	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
#endif

    	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    	{
    		fprintf(stderr, "RGB pixel format is not supported.\n");
    		exit(0);
    	}

	if(!emuThread && !mainThread)
    	{
        	mainThread = co_active();
        	emuThread = co_create(65536*sizeof(void*), retro_wrap_emulator);
    	}

}

void retro_deinit(void)
{
   if(emuThread)
   { 
      co_delete(emuThread);
      emuThread = 0;
   }

   LOGI("Retro DeInit\n");
}

void retro_reset (void) {}

void retro_run (void)
{
	retro_poll_mame_input();

	if (draw_this_frame)
      		video_cb(videoBuffer,rtwi, rthe, topw << PITCH);
   	else
      		video_cb(NULL,rtwi, rthe, topw << PITCH); 

	co_switch(emuThread);
}

void prep_retro_rotation(int rot)
{
   LOGI("Rotation:%d\n",rot);
   environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &rot);
}

static void keyboard_cb(bool down, unsigned keycode, uint32_t character, uint16_t mod)
{
#ifdef KEYDBG
   printf( "Down: %s, Code: %d, Char: %u, Mod: %u. \n",
         down ? "yes" : "no", keycode, character, mod);
#endif
   if (keycode>=320);
   else
   {
      if(down && keycode==RETROK_LSHIFT)
      {
         SHIFTON=-SHIFTON;					
         if(SHIFTON == 1)
            retrokbd_state[keycode]=1;
         else
            retrokbd_state[keycode]=0;	
      }
      else if(keycode!=RETROK_LSHIFT)
      {
         if (down)
            retrokbd_state[keycode]=1;	
         else if (!down)
            retrokbd_state[keycode]=0;
      }
   }
}

bool retro_load_game(const struct retro_game_info *info) 
{
	struct retro_keyboard_callback cb = { keyboard_cb };
    	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cb);

#ifdef M16B
	memset(videoBuffer,0,1024*1024*2);
#else
	memset(videoBuffer,0,1024*1024*2*2);
#endif
	char basename[128];
	extract_basename(basename, info->path, sizeof(basename));
  	extract_directory(g_rom_dir, info->path, sizeof(g_rom_dir));
	strcpy(RPATH,info->path);

	return 1;
}

void retro_unload_game(void)
{
	if(pauseg==0)
   {
		pauseg=-1;				
      co_switch(emuThread);
	}

	LOGI("Retro unload_game\n");	
}

// Stubs
size_t retro_serialize_size(void){ return 0; }
bool retro_serialize(void *data, size_t size){ return false; }
bool retro_unserialize(const void * data, size_t size){ return false; }

unsigned retro_get_region (void) {return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned type) {return 0;}
size_t retro_get_memory_size(unsigned type) {return 0;}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info){return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2){}
void retro_set_controller_port_device(unsigned in_port, unsigned device){}
