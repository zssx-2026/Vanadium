#ifndef VANADIUM_VD_H
#define VANADIUM_VD_H
/* Public C ABI of the Vanadium kernel. The Rust shell links against this. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VD_ABI_VERSION 1

typedef enum vd_event_kind {
  VD_EV_LINE = 0,          /* a = text, permanent line */
  VD_EV_STATUS = 1,        /* a = key, b = text, replaceable line (\r) */
  VD_EV_PROGRESS = 2,      /* a = phase, i1 = done, i2 = total, i3 = bytes, i4 = flags */
  VD_EV_ERROR = 3,         /* a = localized message, i1 = code */
  VD_EV_PROMPT = 4,        /* a = question, b = hint; callback returns 1 for yes */
  VD_EV_SUMMARY = 5,       /* a = json summary */
  VD_EV_STATUS_CLEAR = 6,  /* clear the animated status line */
  VD_EV_TITLE = 7,         /* a = window title / phase title */
  VD_EV_WARN = 8           /* a = warning line */
} vd_event_kind;

typedef struct vd_event {
  int kind;
  const char* a;
  const char* b;
  long long i1, i2, i3, i4;
} vd_event;

/* Return 1 = yes / accepted, 0 = no, -1 = cancel (Ctrl+C). */
typedef int (*vd_event_cb)(void* user, const vd_event* ev);

typedef struct vd_context vd_context;

vd_context* vd_create(const char* home_dir, const char* lang);
void        vd_destroy(vd_context* ctx);
int         vd_run(vd_context* ctx, int argc, const char** argv, vd_event_cb cb, void* user);
const char* vd_version(void);
const char* vd_abi(void);
/* Self-test entry: filter may be NULL. Returns number of failures. */
int         vd_self_test(const char* filter, vd_event_cb cb, void* user);

#ifdef __cplusplus
}
#endif
#endif /* VANADIUM_VD_H */
