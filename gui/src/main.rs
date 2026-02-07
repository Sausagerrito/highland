use eframe::egui;
use egui_plot::{Line, Plot, PlotPoints};
use serialport;
use std::io::{BufRead, BufReader, Write};
use std::sync::mpsc::{Receiver, Sender, TryRecvError};
use std::thread;
use std::time::{Duration, Instant};

fn main() {
    let native_options = eframe::NativeOptions::default();
    let _ = eframe::run_native(
        "Highland Plastics",
        native_options,
        Box::new(|cc| Ok(Box::new(MyApp::new(cc)))),
    );
}

#[derive(Default)]
struct Data {
    history: Vec<[f64; 2]>,
}

#[derive(Default, PartialEq)]
enum Status {
    #[default]
    Idle,
    Running,
}

struct MyApp {
    target_temp: u32,
    target_time: u32,
    status: Status,
    data: Data,
    rx_data: Receiver<(f64, f64)>, // Receiver for Data
    tx_cmd: Sender<String>,        // Sender for Commands
    start_time: Option<Instant>,
}

impl MyApp {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        // Channel 1: Thread -> GUI (Data)
        let (tx_data, rx_data) = std::sync::mpsc::channel::<(f64, f64)>();
        // Channel 2: GUI -> Thread (Commands)
        let (tx_cmd, rx_cmd) = std::sync::mpsc::channel::<String>();

        thread::spawn(move || {
            let port_result = serialport::new("/dev/cu.usbmodem190622201", 9600)
                .timeout(Duration::from_millis(1000))
                .open();

            match port_result {
                Ok(port) => {
                    let mut port_writer = port.try_clone().expect("Failed to clone port");
                    let mut reader = BufReader::new(port);
                    let start_instant = Instant::now();

                    loop {
                        // FIX #1: Don't unwrap! Check if a command exists safely.
                        if let Ok(command) = rx_cmd.try_recv() {
                            let _ = port_writer.write_all(command.as_bytes());
                            let _ = port_writer.flush();
                        }

                        let mut serial_buf = String::new();
                        match reader.read_line(&mut serial_buf) {
                            Ok(_) => {
                                if let Ok(temperature) = serial_buf.trim().parse::<f64>() {
                                    let time_elapsed = start_instant.elapsed().as_secs_f64();
                                    if tx_data.send((time_elapsed, temperature)).is_err() {
                                        break;
                                    }
                                }
                            }
                            Err(e) => {
                                if e.kind() == std::io::ErrorKind::TimedOut {
                                    continue;
                                }
                                eprintln!("Serial Error: {:?}", e);
                                break;
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Failed to open serial port: {}", e);
                }
            }
        });

        // FIX #2: Correctly initialize the struct fields
        Self {
            target_temp: 0,
            target_time: 0,
            status: Status::Idle,
            data: Data::default(),
            rx_data, // Matches struct field name (was 'rx')
            tx_cmd,  // Added this missing field
            start_time: None,
        }
    }
}

impl eframe::App for MyApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        loop {
            // FIX #3: Use 'rx_data', not 'rx'
            match self.rx_data.try_recv() {
                Ok((time, temp)) => {
                    self.data.history.push([time, temp]);
                }
                Err(TryRecvError::Empty) => break,
                Err(TryRecvError::Disconnected) => break,
            }
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("Test Control");
            ui.horizontal(|ui| {
                ui.add(egui::Slider::new(&mut self.target_temp, 0..=1400).text("Target Temp (°C)"));
                ui.add(egui::Slider::new(&mut self.target_time, 0..=60).text("Duration (min)"));
            });

            if ui
                .button(if self.status == Status::Running {
                    "Stop Test"
                } else {
                    "Start Test"
                })
                .clicked()
            {
                if self.status == Status::Idle {
                    self.data.history.clear();
                    self.status = Status::Running;
                    self.start_time = Some(Instant::now());

                    // FIX #4: Actually send the command!
                    let _ = self.tx_cmd.send("start\n".to_string());
                } else {
                    self.status = Status::Idle;

                    // FIX #4: Actually send the command!
                    let _ = self.tx_cmd.send("stop\n".to_string());
                }
            }

            let points: PlotPoints = self.data.history.iter().copied().collect();
            // FIX #5: Correct syntax for Line
            let line = Line::new("temp", points);

            Plot::new("temperature_plot")
                .height(300.0)
                .view_aspect(2.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(line);
                });
        });

        ctx.request_repaint();
    }
}
