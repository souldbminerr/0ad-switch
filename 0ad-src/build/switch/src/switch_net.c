#include <switch.h>

#include <sodium.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t sw_rb_random(void) {
  uint32_t v;
  randomGet(&v, sizeof(v));
  return v;
}

static void sw_rb_buf(void *const buf, const size_t size) {
  randomGet(buf, size);
}

static uint32_t sw_rb_uniform(const uint32_t upper_bound) {
  if (upper_bound < 2)
    return 0;
  const uint32_t min = (uint32_t)(-upper_bound) % upper_bound;
  uint32_t r;
  do
    r = sw_rb_random();
  while (r < min);
  return r % upper_bound;
}

static const char *sw_rb_name(void) { return "switch_csrng"; }
static void sw_rb_stir(void) {}
static int sw_rb_close(void) { return 0; }

static const randombytes_implementation s_switchRandombytes = {
    .implementation_name = sw_rb_name,
    .random = sw_rb_random,
    .stir = sw_rb_stir,
    .uniform = sw_rb_uniform,
    .buf = sw_rb_buf,
    .close = sw_rb_close,
};

void userAppInit(void) {
  socketInitializeDefault();

  randombytes_set_implementation(
      (randombytes_implementation *)&s_switchRandombytes);
  sodium_init();
}

void userAppExit(void) { socketExit(); }
