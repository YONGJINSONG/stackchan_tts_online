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

  // Find the requested mod first.  The old implementation called pause() on
  // every mod it passed while rotating the deque.  That means a direct jump
  // from Realtime to KidsTutor also ran the inactive photo-frame cleanup
  // (timers/SD state) and other side effects during one transition.
  ModBase* target = nullptr;
  const size_t n = modList.size();
  for (size_t i = 0; i < n; i++) {
    if (modList[i]->getName() == name) {
      target = modList[i];
      break;
    }
  }
  if (target == nullptr) {
    Serial.printf("[mod] change_mod_named: '%s' not found\n", name);
    return modList[0];
  }

  ModBase* outgoing = modList[0];
  Serial.printf("[mod] transition %s -> %s: pausing active mod\n",
                outgoing->getName().c_str(), target->getName().c_str());
  outgoing->pause();

  while (modList[0] != target) {
    ModBase* cur = modList[0];
    modList.pop_front();
    modList.push_back(cur);
  }

  avatar.setFaceOffsetX(0);
  Serial.printf("[mod] transition %s -> %s: initializing target\n",
                outgoing->getName().c_str(), modList[0]->getName().c_str());
  modList[0]->init();
  return modList[0];
}

ModBase* change_mod_chatbot(void)
{
#if defined(REALTIME_API)
  return change_mod_named("RealtimeAI");
#else
  return change_mod_named("AiStackChan");
#endif
}

ModBase* get_current_mod(void)
{
  return modList[0];
}

