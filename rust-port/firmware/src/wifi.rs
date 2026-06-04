//! Wi-Fi station bring-up (esp-wifi).
//!
//! Ports the *connectivity* role of the C++ Wi-Fi/Calibre feature: initialise the
//! radio, configure station mode, and connect to an access point (the Calibre
//! smart-device protocol on top is a separate, larger port). Runs as its own
//! embassy task so it doesn't block the reader loop.
//!
//! NOTE: esp-wifi mandates a global allocator (set up in `main`) and a
//! pre-release esp-hal — see the README. Credentials are placeholders here; a
//! real build would read them from an SD config file.

use embassy_executor::Spawner;
use embassy_time::{Duration, Timer};
use esp_hal::peripherals::{RADIO_CLK, RNG, WIFI};
use esp_hal::rng::Rng;
use esp_hal::timer::systimer::Alarm;
use esp_wifi::wifi::{ClientConfiguration, Configuration, WifiController, WifiEvent};
use esp_wifi::EspWifiController;
use static_cell::StaticCell;

/// The esp-wifi controller must outlive the `WifiController`, so it lives here.
static ESP_WIFI: StaticCell<EspWifiController<'static>> = StaticCell::new();

/// Bring up the radio and spawn the station-connection task.
pub fn init(timer: Alarm, rng: RNG, radio_clk: RADIO_CLK, wifi: WIFI, spawner: Spawner) {
    let rng = Rng::new(rng);
    let inited = esp_wifi::init(timer, rng, radio_clk).expect("esp-wifi init");
    let inited: &'static EspWifiController<'static> = ESP_WIFI.init(inited);
    let (controller, _interfaces) = esp_wifi::wifi::new(inited, wifi).expect("esp-wifi new");
    spawner.spawn(connection(controller)).ok();
}

/// Configure station mode and keep a connection to the AP up (retry on drop).
#[embassy_executor::task]
async fn connection(mut controller: WifiController<'static>) {
    // Placeholder credentials — a real build reads these from an SD config.
    let config = Configuration::Client(ClientConfiguration {
        ssid: "crosspoint".try_into().unwrap_or_default(),
        password: "password".try_into().unwrap_or_default(),
        ..Default::default()
    });
    if controller.set_configuration(&config).is_err() {
        return;
    }
    let _ = controller.start_async().await;
    esp_println::println!("wifi: station started");

    loop {
        match controller.connect_async().await {
            Ok(()) => {
                esp_println::println!("wifi: connected");
                // Stay connected until the AP drops us, then reconnect.
                controller.wait_for_event(WifiEvent::StaDisconnected).await;
                esp_println::println!("wifi: disconnected");
            }
            Err(_) => {
                Timer::after(Duration::from_secs(5)).await;
            }
        }
    }
}
