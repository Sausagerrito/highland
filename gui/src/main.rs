use eframe::egui;
use egui_plot;
use serialport;
use std::io::{BufRead, BufReader};
use std::sync::mpsc::{Receiver, Sender};
use std::thread;
use std::time::Duration;

fn main() {
    let native_options = eframe::NativeOptions::default();
    let _ = eframe::run_native(
        "Highland Plastics",
        native_options,
        Box::new(|cc| Ok(Box::new(MyApp::new(cc)))),
    );
}

#[derive(Default)]
enum Heat {
    #[default]
    Cold,
    Heating,
    Cooling,
    Hot,
}

#[derive(Default)]
struct Data {
    name: String,
    uid: u32,
    history: Vec<(f64, f64)>,
}

#[derive(Default, PartialEq)]
enum Status {
    #[default]
    Idle,
    Running,
    Stopping,
}

struct MyApp {
    target_temp: u32,
    target_time: u32,
    status: Status,
    data: Data,
    rx: Receiver<(f64, f64)>,
}

#[derive(Clone, Copy, PartialEq)]
pub struct Graph {
    line_style: egui_plot::LineStyle,
}

impl Default for Graph {
    fn default() -> Self {
        Self {
            line_style: egui_plot::LineStyle::Solid,
        }
    }
}

impl MyApp {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        let (tx, rx) = std::sync::mpsc::channel();

        thread::spawn(move || {
            let port = serialport::new("/dev/ttyUSB0", 9600)
                .timeout(Duration::from_millis(1000))
                .open()
                .unwrap();

            let mut reader = BufReader::new(port);
            let mut time_counter = 0.0;

            loop {
                let mut serial_buf = String::new();
                reader.read_line(&mut serial_buf).unwrap();

                let temperature: f64 = serial_buf.trim().parse().unwrap_or(0.0);

                if tx.send((time_counter, temperature)).is_err() {
                    break;
                }
                time_counter += 1.0;
            }
        });

        Self {
            target_temp: 0,
            target_time: 0,
            status: Status::Idle,
            data: Data::default(),
            rx,
        }
    }
}

impl eframe::App for MyApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        while let Ok(new_point) = self.rx.try_recv() {
            if self.status == Status::Running {
                self.data.history.push(new_point);
            }
        }

        let points: egui_plot::PlotPoints = self.data.history.iter().map(|&p| [p.0, p.1]).collect();

        let line = egui_plot::Line::new("temp", points);

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("Test:");
            ui.add(
                egui::Slider::new(&mut self.target_temp, 0..=1400)
                    .text("Temperature Target (degrees C)"),
            );
            ui.add(egui::Slider::new(&mut self.target_time, 0..=60).text("Test Time (minutes)"));

            if ui.button("Start Test").clicked() {
                self.data.history.clear();
                self.status = Status::Running;
            }

            egui_plot::Plot::new("temperature_plot")
                .height(300.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(line);
                });
        });

        if self.status == Status::Running {
            ctx.request_repaint();
        }
    }
}
