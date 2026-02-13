use crate::model::*;
use eframe::{
    App, Frame,
    egui::{CentralPanel, Context, Slider},
};
use std::sync::mpsc::{Receiver, Sender};

pub struct MyApp {
    pub state: ModelState,
    pub rx_data: Receiver<(f64, f64)>,
    pub tx_cmd: Sender<String>,
}

impl MyApp {
    pub fn new(tx_cmd: Sender<String>, rx_data: Receiver<(f64, f64)>) -> Self {
        Self {
            state: ModelState::new(),
            rx_data,
            tx_cmd,
        }
    }

    fn update_data(&mut self) {
        while let Ok((time, temperature)) = self.rx_data.try_recv() {
            self.state.data.history.push([time, temperature]);
        }
    }
}

impl App for MyApp {
    fn update(&mut self, ctx: &Context, _frame: &mut Frame) {
        self.update_data();

        CentralPanel::default().show(ctx, |ui| {
            ui.heading("Test Control");
            ui.horizontal(|ui| {
                ui.add(
                    Slider::new(&mut self.state.target_temperature, 0..=1400)
                        .text("Target Temperature (°C)"),
                );
                ui.add(Slider::new(&mut self.state.target_time, 0..=60).text("Duration (min)"));
            });

            if ui.button("Start Test").clicked() {};
        });

        ctx.request_repaint();
    }
}
