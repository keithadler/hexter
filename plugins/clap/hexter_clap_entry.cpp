/* hexter - clap_entry for clap-wrapper's clap-first builds (Audio Unit)
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 */
#include <clap/clap.h>
#include "hexter_clap_entry.h"

extern "C"
{
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
  const CLAP_EXPORT struct clap_plugin_entry clap_entry = {CLAP_VERSION, hexter_clap_init,
                                                           hexter_clap_deinit, hexter_clap_get_factory};
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
