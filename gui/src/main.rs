use eframe::egui;

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
}

impl MyApp {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        Self::default()
    }
}

impl eframe::App for MyApp {
    fn update(&mut self, ctx: &egui::Context, frame: &mut eframe::Frame) {
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
        });
    }
}
