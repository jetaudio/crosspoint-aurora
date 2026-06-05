// x4-emu networking shim — see emu_net.h. Active only with -DX4EMU_ETH.
#ifdef X4EMU_ETH
#include "emu_net.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

// Vendored drivers (lib/EmuNet/src/esp_eth_mac_openeth.c, esp_eth_phy_dp83848.c).
extern "C" esp_eth_mac_t *esp_eth_mac_new_openeth(const eth_mac_config_t *config);
extern "C" esp_eth_phy_t *esp_eth_phy_new_dp83848(const eth_phy_config_t *config);

static const char *TAG = "emu_net";

// Bring up the OpenCores Ethernet MAC (QEMU) + DP83848 PHY (QEMU emulates it),
// attach to an esp_netif with DHCP. SLIRP (-nic user) then NATs to host internet.
// Non-blocking: link-up + DHCP happen asynchronously via the eth event loop.
void emuNetStart(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "event loop: %s", esp_err_to_name(e));
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = -1;   // no PHY reset pin (and never touch e-ink RST=GPIO5)
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_cfg);

    if (!mac || !phy) {
        ESP_LOGE(TAG, "mac/phy alloc failed");
        return;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    if (esp_eth_driver_install(&eth_cfg, &eth) != ESP_OK) {
        ESP_LOGE(TAG, "driver install failed");
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)));
    e = esp_eth_start(eth);
    ESP_LOGI(TAG, "openeth start: %s (DHCP via SLIRP follows)", esp_err_to_name(e));
}
#endif  // X4EMU_ETH
