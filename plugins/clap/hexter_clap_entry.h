/* hexter CLAP entry functions, for static linking by clap-wrapper */
#ifndef _HEXTER_CLAP_ENTRY_H
#define _HEXTER_CLAP_ENTRY_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool        hexter_clap_init(const char *plugin_path);
void        hexter_clap_deinit(void);
const void *hexter_clap_get_factory(const char *factory_id);
#ifdef __cplusplus
}
#endif
#endif
