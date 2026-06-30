#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <curl/curl.h>

#include <archive.h>
#include <archive_entry.h>

#include <dirent.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GAME_NRO_URL                                                           \
  "https://github.com/souldbminerr/0ad-switch/releases/latest/download/"       \
  "pyrogenesis.nro"
#define DATA_URL                                                               \
  "https://releases.wildfiregames.com/0ad-0.28.0-unix-data.tar.xz"

#define SD_ROOT "/switch/0ad"
#define GAME_NRO_DST SD_ROOT "/0ad.nro"
#define DATA_DIR SD_ROOT "/data"
#define CONFIG_DST DATA_DIR "/config/default.cfg"
#define CONFIG_SRC "romfs:/config/default.cfg"
#define DATA_TMP SD_ROOT "/_0ad_data.tar.xz"

#define SCR_W 1280
#define SCR_H 720

mode_t umask(mode_t mask) {
  (void)mask;
  return 0;
}

enum { PH_IDLE, PH_NRO, PH_DATA, PH_EXTRACT, PH_CONFIG, PH_DONE, PH_ERROR };
enum { ACT_INSTALL, ACT_UPDATE, ACT_UNINSTALL };

static volatile int g_action;

typedef struct {
  pthread_mutex_t lock;
  volatile int phase;
  volatile float progress;
  volatile bool finished;
  char status[256];
} Progress;

static Progress g_prog;

static void set_status(const char *s) {
  pthread_mutex_lock(&g_prog.lock);
  strncpy(g_prog.status, s, sizeof(g_prog.status) - 1);
  g_prog.status[sizeof(g_prog.status) - 1] = '\0';
  pthread_mutex_unlock(&g_prog.lock);
}

static void get_status(char *out, size_t n) {
  pthread_mutex_lock(&g_prog.lock);
  strncpy(out, g_prog.status, n - 1);
  out[n - 1] = '\0';
  pthread_mutex_unlock(&g_prog.lock);
}

static void mkpath(const char *path) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; ++p)
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0777);
      *p = '/';
    }
  mkdir(tmp, 0777);
}

static bool copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in)
    return false;
  FILE *out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  char buf[16384];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    fwrite(buf, 1, n, out);
  fclose(in);
  fclose(out);
  return true;
}

// Recursively delete a file or directory tree.
static void rmrf(const char *path) {
  DIR *d = opendir(path);
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        continue;
      char p[1024];
      snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
      struct stat st;
      if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
        rmrf(p);
      else
        remove(p);
    }
    closedir(d);
    rmdir(path);
  } else {
    remove(path);
  }
}

static int xfer_cb(void *p, curl_off_t dltotal, curl_off_t dlnow,
                   curl_off_t ult, curl_off_t ulnow) {
  (void)p;
  (void)ult;
  (void)ulnow;
  g_prog.progress = (dltotal > 0) ? (float)dlnow / (float)dltotal : -1.0f;
  return 0;
}

static bool download(const char *url, const char *dst) {
  FILE *f = fopen(dst, "wb");
  if (!f)
    return false;
  CURL *c = curl_easy_init();
  if (!c) {
    fclose(f);
    return false;
  }
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "0ad-switch-installer/1.0");
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
  CURLcode r = curl_easy_perform(c);
  curl_easy_cleanup(c);
  fclose(f);
  return r == CURLE_OK;
}

static bool extract_data(const char *tarpath) {
  struct stat st;
  off_t total = (stat(tarpath, &st) == 0) ? st.st_size : 0;

  struct archive *a = archive_read_new();
  archive_read_support_filter_all(a); // xz, gz, bz2, ...
  archive_read_support_format_all(a); // tar, zip, ...
  if (archive_read_open_filename(a, tarpath, 1 << 16) != ARCHIVE_OK) {
    archive_read_free(a);
    return false;
  }

  struct archive *ext = archive_write_disk_new();
  archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME |
                                          ARCHIVE_EXTRACT_SECURE_NODOTDOT);

  struct archive_entry *entry;
  int count = 0;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char *p = archive_entry_pathname(entry);
    const char *sub = p ? strstr(p, "binaries/data/") : NULL;
    if (!sub)
      continue;
    sub += strlen("binaries/data/");
    if (*sub == '\0')
      continue;

    char dest[1024];
    snprintf(dest, sizeof(dest), "%s/%s", DATA_DIR, sub);
    archive_entry_set_pathname(entry, dest);

    if (archive_write_header(ext, entry) == ARCHIVE_OK) {
      const void *buff;
      size_t size;
      la_int64_t off;
      int r;
      while ((r = archive_read_data_block(a, &buff, &size, &off)) == ARCHIVE_OK)
        archive_write_data_block(ext, buff, size, off);
      archive_write_finish_entry(ext);
    }

    if (total > 0)
      g_prog.progress = (float)archive_filter_bytes(a, -1) / (float)total;
    if ((++count % 64) == 0) {
      char buf[128];
      snprintf(buf, sizeof(buf), "Installing game data... %d files", count);
      set_status(buf);
    }
  }

  archive_read_close(a);
  archive_read_free(a);
  archive_write_close(ext);
  archive_write_free(ext);
  return count > 0;
}

static void *install_thread(void *arg) {
  (void)arg;

  if (g_action == ACT_UNINSTALL) {
    g_prog.phase = PH_EXTRACT;
    g_prog.progress = -1.0f;
    set_status("Removing 0 A.D. ...");
    rmrf(SD_ROOT);
    g_prog.phase = PH_DONE;
    set_status("0 A.D. has been removed.");
    g_prog.finished = true;
    return NULL;
  }

  if (g_action == ACT_UPDATE) {
    mkpath(SD_ROOT);
    g_prog.phase = PH_NRO;
    g_prog.progress = -1.0f;
    set_status("Updating 0 A.D...");
    if (!download(GAME_NRO_URL, GAME_NRO_DST)) {
      set_status("Failed to download 0ad.nro. Check your connection.");
      g_prog.phase = PH_ERROR;
      g_prog.finished = true;
      return NULL;
    }
    g_prog.phase = PH_DONE;
    g_prog.progress = 1.0f;
    set_status("Update complete!");
    g_prog.finished = true;
    return NULL;
  }

  mkpath(DATA_DIR "/config");
  mkpath(DATA_DIR "/mods");

  g_prog.phase = PH_NRO;
  g_prog.progress = -1.0f;
  set_status("Downloading 0 A.D.");
  if (!download(GAME_NRO_URL, GAME_NRO_DST)) {
    set_status("Failed to download 0 A.D, Check your connection.");
    g_prog.phase = PH_ERROR;
    g_prog.finished = true;
    return NULL;
  }

  g_prog.phase = PH_DATA;
  g_prog.progress = -1.0f;
  set_status("Downloading game data...");
  if (!download(DATA_URL, DATA_TMP)) {
    set_status("Failed to download game data.");
    g_prog.phase = PH_ERROR;
    g_prog.finished = true;
    return NULL;
  }

  g_prog.phase = PH_EXTRACT;
  g_prog.progress = 0.0f;
  set_status("Installing game data...");
  if (!extract_data(DATA_TMP)) {
    set_status("Failed to extract game data.");
    g_prog.phase = PH_ERROR;
    g_prog.finished = true;
    return NULL;
  }
  remove(DATA_TMP);

  g_prog.phase = PH_CONFIG;
  g_prog.progress = -1.0f;
  set_status("Writing configuration...");
  copy_file(CONFIG_SRC, CONFIG_DST);

  g_prog.phase = PH_DONE;
  g_prog.progress = 1.0f;
  set_status("Installation complete!");
  g_prog.finished = true;
  return NULL;
}

static SDL_Texture *render_text(SDL_Renderer *r, TTF_Font *f, const char *s,
                                SDL_Color col, int *w, int *h) {
  SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, col);
  if (!surf)
    return NULL;
  SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
  if (w)
    *w = surf->w;
  if (h)
    *h = surf->h;
  SDL_FreeSurface(surf);
  return t;
}

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *s, int cx,
                      int y, SDL_Color col) {
  int w, h;
  SDL_Texture *t = render_text(r, f, s, col, &w, &h);
  if (!t)
    return;
  SDL_Rect dst = {cx - w / 2, y, w, h};
  SDL_RenderCopy(r, t, NULL, &dst);
  SDL_DestroyTexture(t);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  socketInitializeDefault();
  romfsInit();
  curl_global_init(CURL_GLOBAL_DEFAULT);
  pthread_mutex_init(&g_prog.lock, NULL);
  set_status("Ready to install 0 A.D.");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    return 1;
  IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
  TTF_Init();

  SDL_Window *win = SDL_CreateWindow("0ad-installer", 0, 0, SCR_W, SCR_H, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(
      win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  SDL_Texture *logo = IMG_LoadTexture(ren, "romfs:/logo.png");
  int logoW = 0, logoH = 0;
  if (logo)
    SDL_QueryTexture(logo, NULL, NULL, &logoW, &logoH);
  TTF_Font *font = TTF_OpenFont("romfs:/font.ttf", 26);
  TTF_Font *small = TTF_OpenFont("romfs:/font.ttf", 20);

  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i)) {
      SDL_GameControllerOpen(i);
      break;
    }

  const SDL_Color cParchment = {232, 220, 192, 255};
  const SDL_Color cGold = {200, 160, 80, 255};
  const SDL_Color cDim = {150, 140, 120, 255};
  const SDL_Color cRed = {200, 90, 70, 255};

  bool started = false;
  bool running = true;
  bool confirmUninstall = false;
  int sel = 0; // 0=Install, 1=Update, 2=Uninstall
  const char *items[3] = {"Install", "Update (game only)", "Uninstall"};
  pthread_t worker;

  while (running && appletMainLoop()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        running = false;
      else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        int b = e.cbutton.button;
        // Nintendo-labelled: A = confirm (SDL physical B / right),
        // B = back/cancel (SDL physical A / bottom), (+) = exit.
        bool confirm = (b == SDL_CONTROLLER_BUTTON_B);
        bool back = (b == SDL_CONTROLLER_BUTTON_A);

        if (b == SDL_CONTROLLER_BUTTON_START) {
          if (!started || g_prog.finished)
            running = false;
        } else if (!started && !confirmUninstall) {
          // Menu: choose an action.
          if (b == SDL_CONTROLLER_BUTTON_DPAD_UP)
            sel = (sel + 3 - 1) % 3;
          else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            sel = (sel + 1) % 3;
          else if (confirm) {
            if (sel == 2)
              confirmUninstall = true; // ask before deleting
            else {
              g_action = (sel == 0) ? ACT_INSTALL : ACT_UPDATE;
              started = true;
              pthread_create(&worker, NULL, install_thread, NULL);
            }
          }
        } else if (confirmUninstall) {
          if (confirm) {
            confirmUninstall = false;
            g_action = ACT_UNINSTALL;
            started = true;
            pthread_create(&worker, NULL, install_thread, NULL);
          } else if (back)
            confirmUninstall = false;
        } else if (started && g_prog.finished && g_prog.phase == PH_DONE) {
          if (confirm && (g_action == ACT_INSTALL || g_action == ACT_UPDATE) &&
              envHasNextLoad()) {
            envSetNextLoad(GAME_NRO_DST, "0ad.nro");
            running = false;
          }
        }
      }
    }

    SDL_SetRenderDrawColor(ren, 22, 18, 14, 255);
    SDL_RenderClear(ren);

    if (logo && logoW > 0) {
      int targetW = 760;
      int targetH = (int)((float)logoH * (float)targetW / (float)logoW);
      SDL_Rect dst = {(SCR_W - targetW) / 2, 60, targetW, targetH};
      SDL_RenderCopy(ren, logo, NULL, &dst);
    }

    draw_text(ren, font, "Release 28", SCR_W / 2, 470, cGold);

    char status[256];
    get_status(status, sizeof(status));

    if (!started && confirmUninstall) {
      draw_text(ren, font, "Remove all installed 0 A.D. files?", SCR_W / 2, 520,
                cParchment);
      draw_text(ren, small, "This deletes /switch/0ad (game, data and config).",
                SCR_W / 2, 560, cDim);
      draw_text(ren, font, "(A) Confirm        (B) Cancel", SCR_W / 2, 600,
                cGold);
    } else if (!started) {
      for (int i = 0; i < 3; ++i)
        draw_text(ren, font, items[i], SCR_W / 2, 505 + i * 44,
                  i == sel ? cGold : cParchment);
      draw_text(ren, small, "(Up/Down) Select      (A) Confirm      (+) Exit",
                SCR_W / 2, 650, cDim);
    } else {
      SDL_Color sc = (g_prog.phase == PH_ERROR) ? cRed : cParchment;
      draw_text(ren, font, status, SCR_W / 2, 540, sc);

      // Progress bar
      const int barW = 760, barH = 26;
      const int barX = (SCR_W - barW) / 2, barY = 585;
      SDL_Rect track = {barX, barY, barW, barH};
      SDL_SetRenderDrawColor(ren, 50, 42, 32, 255);
      SDL_RenderFillRect(ren, &track);

      float p = g_prog.progress;
      if (g_prog.phase == PH_DONE)
        p = 1.0f;
      if (p >= 0.0f) {
        SDL_Rect fill = {barX, barY, (int)(barW * (p > 1 ? 1 : p)), barH};
        SDL_SetRenderDrawColor(ren, 200, 160, 80, 255);
        SDL_RenderFillRect(ren, &fill);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", (int)((p > 1 ? 1 : p) * 100));
        draw_text(ren, small, pct, SCR_W / 2, barY + barH + 8, cDim);
      } else {
        draw_text(ren, small, "Working...", SCR_W / 2, barY + barH + 8, cDim);
      }
      SDL_SetRenderDrawColor(ren, 200, 160, 80, 255);
      SDL_RenderDrawRect(ren, &track);

      if (g_prog.finished && g_prog.phase == PH_DONE)
        draw_text(ren, small, "(A) Launch 0 A.D.      (+) Exit", SCR_W / 2, 650,
                  cGold);
      else if (g_prog.finished && g_prog.phase == PH_ERROR)
        draw_text(ren, small, "(+) Exit", SCR_W / 2, 650, cDim);
    }

    SDL_RenderPresent(ren);
  }

  if (started)
    pthread_join(worker, NULL);

  if (logo)
    SDL_DestroyTexture(logo);
  if (font)
    TTF_CloseFont(font);
  if (small)
    TTF_CloseFont(small);
  TTF_Quit();
  IMG_Quit();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  curl_global_cleanup();
  romfsExit();
  socketExit();
  return 0;
}
