#include <pthread.h>
#include <string.h>
#include <switch.h>

volatile int g_switchGlthreadPinned = 0;

extern int __real_pthread_setname_np(pthread_t thread, const char *name);

static u64 switch_process_coremask(void) {
  u64 cm = 0;
  if (R_FAILED(svcGetInfo(&cm, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) ||
      cm == 0)
    cm = 0x7; // cores 0,1,2
  return cm;
}

void switchPinThisThread(int preferred) {
  const u64 cm = switch_process_coremask();
  if (!(cm & (1ull << preferred)))
    preferred = __builtin_ctzll(cm); // first available core
  svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred, cm);
}

int __wrap_pthread_setname_np(pthread_t thread, const char *name) {
  const int r = __real_pthread_setname_np(thread, name);
  if (name && strstr(name, "glthread")) {
    const u64 cm = switch_process_coremask();
    const int pref = (cm & (1ull << 1)) ? 1 : (int)__builtin_ctzll(cm);
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, pref, cm);
    g_switchGlthreadPinned = 1;
  }
  return r;
}
