#include <stdio.h>
#include <assert.h>
#include "Application.h"
#include "../../addons/include/xbmc_scr_dll.h"

extern "C" {

#include "solarwinds.h"

static CUBE_STATE_T _state, *state=&_state;

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!props)
    return ADDON_STATUS_UNKNOWN;

  // Clear application state
  memset( state, 0, sizeof( *state ) );
  // Start OGLES
  assert( g_application.IsCurrentThread() );
  glGetError();

  SCR_PROPS* scrprops = (SCR_PROPS*)props;
  //state->width = scrprops->width;
  //state->height = scrprops->height;
  solarwinds_init(state);
  solarwinds_init_shaders(state);

  return ADDON_STATUS_OK;
}

void Start()
{
}

void Render()
{
  solarwinds_update(state);
  solarwinds_render(state);
}

void ADDON_Stop()
{
   solarwinds_deinit_shaders(state);
}

void ADDON_Destroy()
{
}

ADDON_STATUS ADDON_GetStatus()
{
puts(__func__);
  return ADDON_STATUS_OK;
}

bool ADDON_HasSettings()
{
puts(__func__);
  return false;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
puts(__func__);
  return 0;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
puts(__func__);
  return ADDON_STATUS_OK;
}

void ADDON_FreeSettings()
{
puts(__func__);
}

void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
puts(__func__);
}

void GetInfo(SCR_INFO *info)
{
puts(__func__);
}

void Remove()
{
puts(__func__);
}

}

