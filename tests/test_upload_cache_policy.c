#include <assert.h>
#include "kk_upload_cache_policy.h"
int main(void) {
   assert(!kk_upload_cache_enabled(0));
   assert(!kk_upload_cache_enabled("0"));
   assert(!kk_upload_cache_enabled("10"));
   assert(kk_upload_cache_enabled("1"));
   for (unsigned expanded = 0; expanded < 2; ++expanded) {
      unsigned cached = 0, destroyed = 0;
      for (unsigned completed = 0; completed < 300; ++completed) {
         if (kk_upload_cache_keep(cached, expanded)) ++cached;
         else ++destroyed;
      }
      assert(cached == (expanded ? 128 : 33));
      assert(cached + destroyed == 300);
      while (cached) --cached;
      assert(kk_upload_cache_keep(cached, expanded));
   }
   assert(kk_upload_cache_keep(32, false));
   assert(!kk_upload_cache_keep(33, false));
   assert(kk_upload_cache_keep(127, true));
   assert(!kk_upload_cache_keep(128, true));
   return 0;
}
