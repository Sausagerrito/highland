use eframe::egui;
use egui_plot;

fn main() {
    let native_options = eframe::NativeOptions::default();
    eframe::run_native(
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
enum Status {
    #[default]
    Idle,
    Running,
    Stopping,
}

#[derive(Default)]
struct MyApp {
    temperature: u32,
    time: u32,
    status: Status,
    heat: Heat,
    graph: Graph,
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
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        Self::default()
    }
}

impl eframe::App for MyApp {
    fn update(&mut self, ctx: &egui::Context, frame: &mut eframe::Frame) {
        let data: [[f64; 2]; 6] = [
            [0.0, 20.0],
            [1.0, 100.0],
            [2.0, 400.0],
            [3.0, 800.0],
            [4.0, 1100.0],
            [5.0, 1200.0],
        ];

        let points = egui_plot::PlotPoints::from(data.to_vec());
        let line = egui_plot::Line::new("Points", points);
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("Test:");
            ui.add(
                egui::Slider::new(&mut self.temperature, 0..=1400)
                    .text("Temperature Target (degrees C)"),
            );
            ui.add(egui::Slider::new(&mut self.time, 0..=60).text("Test Time (minutes)"));

            if ui.button("Start Test").clicked() {
                self.status = Status::Running;
            }

            egui_plot::Plot::new("temperature_plot")
                .height(300.0)
                .show(ui, |plot_ui| {
                    plot_ui.line(line);
                });
        });
    }
}
