mod app;
mod model;
mod serial;

use app::MyApp;

fn main() {
    let ports = serial::list_available_ports();
    for (i, name) in ports.iter().enumerate() {
        println!("{}: {}", i, name);
    }

    let (tx_cmd, rx_data) = serial::start_serial_worker("/dev/cu.usbmodem1101");

    let native_options = eframe::NativeOptions::default();
    let _ = eframe::run_native(
        "Highland Plastics",
        native_options,
        Box::new(|_cc| Ok(Box::new(MyApp::new(tx_cmd, rx_data)))),
    );
}
