#pragma once
#include <stdint.h>

// Borrowed QWindow* is valid only for the synchronous callback. The bridge
// creates its platform window first and rejects stale window handles. Return
// nonzero on success; zero propagates a hook failure to the managed caller.
// Capture-exclusion policy and platform APIs belong to the application hook.
typedef int (*GqtNativeWindowHook)(void *window, void *context);

#ifdef __cplusplus
extern "C" {
#endif
int gqt_window_with_native(uint64_t window_id, GqtNativeWindowHook hook, void *context);
#ifdef __cplusplus
}
#endif
