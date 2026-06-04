//! A custom `embassy-time` driver for **stable** esp-hal 1.1.1.
//!
//! `esp-hal-embassy` (the usual time driver) only pairs with pre-release esp-hal,
//! so to run embassy on stable we provide our own: a 1 ms periodic `SystemTimer`
//! alarm advances a millisecond counter and wakes any expired `embassy-time`
//! timers. `TICK_HZ` is 1000 (the `tick-hz-1_000` feature), so the counter *is*
//! the embassy tick count.
//!
//! This keeps the whole project on stable esp-hal while satisfying Step 2's
//! `embassy-executor` + `embassy-time` requirement.

use core::cell::{Cell, RefCell};
use core::task::Waker;

use critical_section::Mutex;
use embassy_time_driver::Driver;
use embassy_time_queue_utils::Queue;
use esp_hal::handler;
use esp_hal::time::Duration;
use esp_hal::timer::systimer::Alarm;
use esp_hal::timer::Timer; // trait: now/load_value/enable_interrupt/…

/// Milliseconds since `init` (== embassy ticks, since TICK_HZ = 1000).
static MILLIS: Mutex<Cell<u64>> = Mutex::new(Cell::new(0));
/// Pending `embassy-time` timers.
static QUEUE: Mutex<RefCell<Queue>> = Mutex::new(RefCell::new(Queue::new()));
/// The 1 ms alarm, kept reachable so the ISR can acknowledge it.
static ALARM: Mutex<RefCell<Option<Alarm>>> = Mutex::new(RefCell::new(None));

struct EspDriver;
embassy_time_driver::time_driver_impl!(static DRIVER: EspDriver = EspDriver);

impl Driver for EspDriver {
    fn now(&self) -> u64 {
        critical_section::with(|cs| MILLIS.borrow(cs).get())
    }

    fn schedule_wake(&self, at: u64, waker: &Waker) {
        critical_section::with(|cs| {
            QUEUE.borrow(cs).borrow_mut().schedule_wake(at, waker);
        });
    }
}

/// 1 ms tick: ack the alarm, advance the clock, wake any expired timers.
#[handler]
fn tick() {
    critical_section::with(|cs| {
        if let Some(alarm) = ALARM.borrow(cs).borrow().as_ref() {
            alarm.clear_interrupt();
        }
        let now = MILLIS.borrow(cs).get() + 1;
        MILLIS.borrow(cs).set(now);
        QUEUE.borrow(cs).borrow_mut().next_expiration(now);
    });
}

/// Start the 1 ms tick on the given alarm. Call once at boot before any
/// `embassy-time` use.
pub fn init(alarm: Alarm) {
    alarm.set_interrupt_handler(tick);
    alarm.enable_auto_reload(true);
    alarm.enable_interrupt(true);
    let _ = alarm.load_value(Duration::from_millis(1));
    alarm.start();
    critical_section::with(|cs| {
        ALARM.borrow(cs).replace(Some(alarm));
    });
}
