#include <string.h>
#include <switch.h>

int switchShowKeyboard(const char *initial, const char *guide, char *out,
                       unsigned long outSize) {
  if (!out || outSize == 0)
    return 0;
  out[0] = '\0';

  SwkbdConfig kbd;
  if (R_FAILED(swkbdCreate(&kbd, 0)))
    return 0;

  swkbdConfigMakePresetDefault(&kbd);
  swkbdConfigSetStringLenMax(&kbd, (u32)(outSize - 1));
  if (initial && initial[0])
    swkbdConfigSetInitialText(&kbd, initial);
  if (guide && guide[0])
    swkbdConfigSetGuideText(&kbd, guide);

  Result rc = swkbdShow(&kbd, out, outSize);
  swkbdClose(&kbd);

  if (R_FAILED(rc)) {
    out[0] = '\0';
    return 0;
  }
  return 1;
}
