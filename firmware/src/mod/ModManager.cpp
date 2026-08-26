#include <Arduino.h>
#include <deque>
#include "ModManager.h"
#include "ModBase.h"
#include <Avatar.h>

using namespace m5avatar;


/// 外部参照 ///
extern Avatar avatar;

///////////////
static bool g_avatar_status = true;
std::deque<ModBase*> modList;


static void avatar_fadeout(bool reverse)
{
  avatar.fadeoutStart(reverse);
  while(!avatar.isFadeoutDone()){
    delay(10);
  }
}

void add_mod(ModBase* mod)
{
  modList.push_back(mod);
}

ModBase* change_mod(bool reverse)
{
  ModBase* mod;
  avatar_fadeout(reverse);
  mod = modList[0];
  mod->pause();
  if(reverse){
    mod = modList.back();
    modList.pop_back();
    modList.push_front(mod);
  }
  else{
    modList.pop_front();
    modList.push_back(mod);
  }
  mod = modList[0];
  avatar.setFaceOffsetX(0);
  mod->init();
  return mod;
}

ModBase* change_mod_named(const char* name)
{
  if (name == nullptr || name[0] == '\0' || modList.empty()) return get_current_mod();
  if (modList[0]->getName() == name) return modList[0];

  const size_t n = modList.size();
  for (size_t i = 0; i < n; i++) {
    ModBase* cur = modList[0];
    cur->pause();
    modList.pop_front();
    modList.push_back(cur);
    if (modList[0]->getName() == name) {
      avatar.setFaceOffsetX(0);
      modList[0]->init();
      return modList[0];
    }
  }
  Serial.printf("[mod] change_mod_named: '%s' not found\n", name);
  avatar.setFaceOffsetX(0);
  modList[0]->init();
  return modList[0];
}

ModBase* get_current_mod(void)
{
  return modList[0];
}

