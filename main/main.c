#include "switches.h"
#include "switch_config.h"

static const char *TAG = "MAIN";

void app_main(void) {

    static switch_ctx_t switch_ctx[SWITCH_CFG_LEN] = {0};

    // switch_cfg is generated in switch_config
    ESP_ERROR_CHECK(switches_init(switch_ctx, switch_cfg, 1));
}