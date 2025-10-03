use eframe::egui;
use std::io::{BufRead, BufReader, Write};
use std::time::Duration;

fn main() {
    let native_options = eframe::NativeOptions::default();
    eframe::run_native(
        "Test App",
        native_options,
        Box::new(|cc| Ok(Box::new(MyEguiApp::new(cc)))),
    );
}

#[derive(Default)]
struct MyEguiApp {
    led1: bool,
    led2: bool,
}

impl MyEguiApp {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        Self::default()
    }
}

impl eframe::App for MyEguiApp {
    fn update(&mut self, ctx: &egui::Context, frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.horizontal(|ui| {
                if ui.button("LED1").clicked() {
                    self.led1 = !self.led1;
                }
                if ui.button("LED2").clicked() {
                    self.led2 = !self.led2;
                }
            });
        });
    }
}
