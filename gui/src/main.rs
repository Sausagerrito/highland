use crossbeam_channel::{Receiver, Sender, unbounded};
use eframe::egui;
use egui_plot::{Corner, HLine, Legend, Line, LineStyle, Plot, PlotPoints};
use std::path::PathBuf;
use std::thread;
use std::time::Duration;

// --- Data Structures ---

#[derive(Clone, Debug, PartialEq)]
enum SystemState {
    Idle,
    Homing,
    Ready,
    Heating,
    Testing,
}

#[derive(Clone, Debug)]
struct Telemetry {
    sys_time: f64,
    test_timer: f64,
    t_shield: f64,
    t_hot: f64,
    t_cold: f64,
    state: SystemState,
}

struct AppState {
    test_name: String,
    export_directory: PathBuf,
    target_temp: String,
    test_duration: String,
    graph_window: String,

    is_recording: bool,
    skip_preheat: bool,
    awaiting_ready: bool,
    test_start_offset: f64,
    current_state: SystemState,
    peak_hot_temp: f64,
    peak_cold_temp: f64,

    history: Vec<Telemetry>,
    test_history: Vec<Telemetry>,

    tx_cmd: Sender<String>,
    rx_telemetry: Receiver<Telemetry>,
}

impl Default for AppState {
    fn default() -> Self {
        let (tx_cmd, _) = unbounded();
        let (_, rx_telemetry) = unbounded();

        let export_directory = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        let mut test_name = "UL2596_Test_01".to_owned();

        let mut path = export_directory.join(format!("{}.csv", test_name));
        while path.exists() {
            test_name = AppState::bump_name(&test_name);
            path = export_directory.join(format!("{}.csv", test_name));
        }

        Self {
            test_name,
            export_directory,
            target_temp: "1400".to_owned(),
            test_duration: "600".to_owned(),
            graph_window: "30".to_owned(),
            is_recording: false,
            skip_preheat: false,
            awaiting_ready: false,
            test_start_offset: 0.0,
            current_state: SystemState::Idle,
            peak_hot_temp: 0.0,
            peak_cold_temp: 0.0,
            history: Vec::with_capacity(1000),
            test_history: Vec::with_capacity(80000),
            tx_cmd,
            rx_telemetry,
        }
    }
}

// --- Font Configuration ---
fn setup_custom_fonts(ctx: &egui::Context) {
    let mut style = (*ctx.global_style()).clone();

    // BUG FIX: Safely insert font sizes instead of wiping the entire style map!
    style.text_styles.insert(
        egui::TextStyle::Heading,
        egui::FontId::new(26.0, egui::FontFamily::Proportional),
    );
    style.text_styles.insert(
        egui::TextStyle::Body,
        egui::FontId::new(16.0, egui::FontFamily::Proportional),
    );
    style.text_styles.insert(
        egui::TextStyle::Monospace,
        egui::FontId::new(14.0, egui::FontFamily::Monospace),
    );
    style.text_styles.insert(
        egui::TextStyle::Button,
        egui::FontId::new(16.0, egui::FontFamily::Proportional),
    );
    style.text_styles.insert(
        egui::TextStyle::Small,
        egui::FontId::new(12.0, egui::FontFamily::Proportional),
    );
    ctx.set_global_style(style);

    let mut fonts = egui::FontDefinitions::default();
    fonts.font_data.insert(
        "ui_font".to_owned(),
        egui::FontData::from_static(include_bytes!(
            "../assets/JetBrainsMonoNerdFont-SemiBold.ttf"
        ))
        .into(),
    );
    fonts
        .families
        .get_mut(&egui::FontFamily::Proportional)
        .unwrap()
        .insert(0, "ui_font".to_owned());
    ctx.set_fonts(fonts);
}

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1150.0, 800.0]),
        ..Default::default()
    };

    let (tx_cmd, rx_cmd) = unbounded::<String>();
    let (tx_telemetry, rx_telemetry) = unbounded::<Telemetry>();

    thread::spawn(move || {
        let mut sim_state = SystemState::Idle;
        let mut target_temp: f64 = 1400.0;
        let mut test_duration: f64 = 60.0;
        let mut t_shield: f64 = 25.0;
        let mut t_hot: f64 = 25.0;
        let mut t_cold: f64 = 25.0;
        let mut sys_time: f64 = 0.0;
        let mut test_timer: f64 = 0.0;
        let dt: f64 = 0.1;

        loop {
            while let Ok(raw_cmd) = rx_cmd.try_recv() {
                let cmd = raw_cmd.trim();

                match cmd {
                    "CMD:HOME" => {
                        sim_state = SystemState::Homing;
                        test_timer = 0.0;
                    }
                    "CMD:START" => {
                        sim_state = SystemState::Heating;
                        test_timer = 0.0;
                    }
                    "CMD:START_SKIP" => {
                        sim_state = SystemState::Testing;
                        test_timer = 0.0;
                    }
                    "CMD:STOP" => {
                        sim_state = SystemState::Idle;
                        test_timer = 0.0;
                    }
                    _ if cmd.starts_with("SET_TEMP:") => {
                        if let Ok(val) = cmd["SET_TEMP:".len()..].parse::<f64>() {
                            target_temp = val;
                        }
                    }
                    _ if cmd.starts_with("SET_TIME:") => {
                        if let Ok(val) = cmd["SET_TIME:".len()..].parse::<f64>() {
                            test_duration = val;
                        }
                    }
                    _ => {}
                }
            }

            sys_time += dt;

            match sim_state {
                SystemState::Idle | SystemState::Ready => {
                    t_shield += (25.0 - t_shield) * 0.1 * dt;
                    t_hot += (25.0 - t_hot) * 0.1 * dt;
                    t_cold += (25.0 - t_cold) * 0.05 * dt;
                    test_timer = 0.0;
                }
                SystemState::Homing => {
                    test_timer += dt;
                    t_shield += (25.0 - t_shield) * 0.1 * dt;
                    t_hot += (25.0 - t_hot) * 0.1 * dt;
                    t_cold += (25.0 - t_cold) * 0.05 * dt;

                    if test_timer >= 2.0 {
                        sim_state = SystemState::Ready;
                        test_timer = 0.0;
                    }
                }
                SystemState::Heating => {
                    t_shield += (target_temp + 50.0 - t_shield) * 0.5 * dt;
                    t_hot += (100.0 - t_hot) * 0.1 * dt;
                    t_cold += (30.0 - t_cold) * 0.05 * dt;

                    if t_shield >= target_temp {
                        t_shield = target_temp;
                        sim_state = SystemState::Testing;
                    }
                }
                SystemState::Testing => {
                    test_timer += dt;
                    t_shield += (25.0 - t_shield) * 0.2 * dt;
                    t_hot += (target_temp + 150.0 - t_hot) * 0.8 * dt;
                    t_cold += (450.0 - t_cold) * 0.03 * dt;

                    if test_timer >= test_duration {
                        sim_state = SystemState::Idle;
                        test_timer = 0.0;
                    }
                }
            }

            let _ = tx_telemetry.send(Telemetry {
                sys_time,
                test_timer,
                t_shield,
                t_hot,
                t_cold,
                state: sim_state.clone(),
            });
            thread::sleep(Duration::from_millis(100));
        }
    });

    let mut app_state = AppState::default();
    app_state.tx_cmd = tx_cmd;
    app_state.rx_telemetry = rx_telemetry;

    eframe::run_native(
        "UL2596 TAG Test Controller",
        options,
        Box::new(|cc| {
            setup_custom_fonts(&cc.egui_ctx);
            egui_extras::install_image_loaders(&cc.egui_ctx);
            Ok(Box::new(app_state))
        }),
    )
}

impl AppState {
    fn bump_name(name: &str) -> String {
        if let Some(pos) = name.rfind('_') {
            let suffix = &name[pos + 1..];
            if let Ok(num) = suffix.parse::<u32>() {
                return format!("{}_{:02}", &name[..pos], num + 1);
            }
        }
        format!("{}_01", name)
    }

    fn auto_export_csv(&mut self) {
        if self.test_history.is_empty() {
            return;
        }

        let mut name_to_save = self.test_name.clone();
        let mut path = self.export_directory.join(format!("{}.csv", name_to_save));

        while path.exists() {
            name_to_save = Self::bump_name(&name_to_save);
            path = self.export_directory.join(format!("{}.csv", name_to_save));
        }

        if let Ok(mut wtr) = csv::Writer::from_path(&path) {
            let _ = wtr.write_record(["Test_Time(s)", "Shield(C)", "Hot(C)", "Cold(C)"]);
            for row in &self.test_history {
                let relative_time = (row.sys_time - self.test_start_offset).max(0.0);
                let _ = wtr.write_record([
                    format!("{:.2}", relative_time),
                    format!("{:.2}", row.t_shield),
                    format!("{:.2}", row.t_hot),
                    format!("{:.2}", row.t_cold),
                ]);
            }
            let _ = wtr.flush();
        }

        self.test_name = Self::bump_name(&name_to_save);
        let mut next_path = self
            .export_directory
            .join(format!("{}.csv", self.test_name));
        while next_path.exists() {
            self.test_name = Self::bump_name(&self.test_name);
            next_path = self
                .export_directory
                .join(format!("{}.csv", self.test_name));
        }
    }
}

impl eframe::App for AppState {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let window_size = self.graph_window.parse::<f64>().unwrap_or(60.0);

        while let Ok(data) = self.rx_telemetry.try_recv() {
            let previous_state = self.current_state.clone();
            self.current_state = data.state.clone();

            if self.is_recording
                && previous_state != SystemState::Idle
                && self.current_state == SystemState::Idle
            {
                self.is_recording = false;
                self.awaiting_ready = false;
                self.auto_export_csv();
            }

            if self.awaiting_ready && self.current_state == SystemState::Ready {
                self.awaiting_ready = false;
                let _ = self.tx_cmd.send("CMD:START\n".to_string());
            }

            if self.current_state == SystemState::Testing {
                if data.t_hot > self.peak_hot_temp {
                    self.peak_hot_temp = data.t_hot;
                }
                if data.t_cold > self.peak_cold_temp {
                    self.peak_cold_temp = data.t_cold;
                }
            }

            self.history.push(data.clone());
            if self.is_recording {
                self.test_history.push(data.clone());
            }

            let cutoff = data.sys_time - (window_size + 2.0);
            self.history.retain(|d| d.sys_time >= cutoff);
        }
        ui.ctx().request_repaint();

        // Using standard egui::SidePanel API
        egui::Panel::left("control_panel")
            .resizable(true)
            .show_inside(ui, |ui| {
                ui.add_space(10.0);

                egui::Frame::group(ui.style()).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.heading(egui::RichText::new("Configuration").strong().size(18.0));
                    ui.add_space(8.0);
                    ui.label("Test Name:");
                    ui.text_edit_singleline(&mut self.test_name);
                    ui.add_space(8.0);
                    ui.label(
                        egui::RichText::new("Target Temp (°C):")
                            .color(egui::Color32::MAGENTA)
                            .strong(),
                    );
                    ui.text_edit_singleline(&mut self.target_temp);
                    ui.add_space(8.0);
                    ui.label("Duration (s):");
                    ui.text_edit_singleline(&mut self.test_duration);
                });

                ui.add_space(20.0);

                egui::Frame::group(ui.style()).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.heading(egui::RichText::new("Execution").strong().size(18.0));
                    ui.add_space(8.0);

                    ui.checkbox(&mut self.skip_preheat, "Skip Preheat");
                    ui.add_space(8.0);

                    if !self.is_recording {
                        if ui
                            .add_sized(
                                [ui.available_width(), 45.0],
                                egui::Button::new(
                                    egui::RichText::new("▶ START SEQUENCE")
                                        .strong()
                                        .color(egui::Color32::GREEN),
                                ),
                            )
                            .clicked()
                        {
                            self.is_recording = true;
                            self.test_history.clear();
                            self.test_start_offset =
                                self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
                            self.peak_hot_temp = 0.0;
                            self.peak_cold_temp = 0.0;

                            let mut path = self
                                .export_directory
                                .join(format!("{}.csv", self.test_name));
                            while path.exists() {
                                self.test_name = AppState::bump_name(&self.test_name);
                                path = self
                                    .export_directory
                                    .join(format!("{}.csv", self.test_name));
                            }

                            let _ = self.tx_cmd.send(format!("SET_TEMP:{}\n", self.target_temp));
                            let _ = self
                                .tx_cmd
                                .send(format!("SET_TIME:{}\n", self.test_duration));

                            if self.skip_preheat {
                                let _ = self.tx_cmd.send("CMD:START_SKIP\n".to_string());
                            } else {
                                self.awaiting_ready = true;
                                let _ = self.tx_cmd.send("CMD:HOME\n".to_string());
                            }
                        }
                    } else {
                        if ui
                            .add_sized(
                                [ui.available_width(), 45.0],
                                egui::Button::new(
                                    egui::RichText::new("⏹ ABORT SEQUENCE")
                                        .strong()
                                        .color(egui::Color32::RED),
                                ),
                            )
                            .clicked()
                        {
                            let _ = self.tx_cmd.send("CMD:STOP\n".to_string());
                        }
                    }

                    ui.add_space(15.0);

                    ui.add_enabled_ui(!self.is_recording, |ui| {
                        ui.label(
                            egui::RichText::new("Export Directory:").color(egui::Color32::GRAY),
                        );

                        // BUG FIX: .truncate() ensures long file paths don't stretch the left panel off the screen
                        ui.add(
                            egui::Label::new(
                                egui::RichText::new(self.export_directory.display().to_string())
                                    .small(),
                            )
                            .truncate(),
                        );

                        ui.add_space(4.0);
                        if ui
                            .add_sized(
                                [ui.available_width(), 30.0],
                                egui::Button::new("󰉖 CHANGE DIRECTORY"),
                            )
                            .clicked()
                        {
                            if let Some(path) = rfd::FileDialog::new().pick_folder() {
                                self.export_directory = path;
                                let mut next_path = self
                                    .export_directory
                                    .join(format!("{}.csv", self.test_name));
                                while next_path.exists() {
                                    self.test_name = AppState::bump_name(&self.test_name);
                                    next_path = self
                                        .export_directory
                                        .join(format!("{}.csv", self.test_name));
                                }
                            }
                        }
                    });

                    ui.add_space(8.0);

                    // Restored the Manual Export button just in case you still need it!
                    ui.add_enabled_ui(!self.is_recording && !self.test_history.is_empty(), |ui| {
                        if ui
                            .add_sized(
                                [ui.available_width(), 35.0],
                                egui::Button::new("󰆓 MANUAL EXPORT"),
                            )
                            .clicked()
                        {
                            self.export_csv();
                        }
                    });
                });
            });

        egui::CentralPanel::default().show_inside(ui, |ui| {
            ui.add_space(10.0);

            // BUG FIX: Reverted to `ui.horizontal` to prevent the header from eating the entire vertical screen
            ui.horizontal(|ui| {
                let heading_height = ui.text_style_height(&egui::TextStyle::Heading);
                ui.add(
                    egui::Image::new(egui::include_image!("../assets/logo.png"))
                        .fit_to_exact_size(egui::vec2(heading_height, heading_height)),
                );

                ui.add_space(4.0);

                ui.heading(egui::RichText::new("UL2596 TAG TEST CONTROLLER").strong());

                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    let (color, text) = match self.current_state {
                        SystemState::Idle | SystemState::Ready => {
                            (egui::Color32::DARK_GRAY, " SYSTEM STANDBY")
                        }
                        SystemState::Homing => (egui::Color32::LIGHT_RED, "󰓾 HOMING ACTUATOR"),
                        SystemState::Heating => (egui::Color32::ORANGE, " HEATING SHIELD"),
                        SystemState::Testing => (egui::Color32::RED, " ACTIVE TEST"),
                    };

                    egui::Frame::new()
                        .fill(color)
                        .corner_radius(4.0)
                        .inner_margin(egui::Margin::symmetric(10, 4))
                        .show(ui, |ui| {
                            ui.label(
                                egui::RichText::new(text)
                                    .color(egui::Color32::WHITE)
                                    .strong(),
                            );
                        });
                    ui.label(egui::RichText::new("Status:").strong());
                });
            });

            ui.separator();
            ui.add_space(10.0);

            let latest_time = self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
            let start_time = latest_time - window_size;

            let shield_pts: PlotPoints = self
                .history
                .iter()
                .filter(|d| d.sys_time >= start_time)
                .map(|d| [d.sys_time, d.t_shield])
                .collect();
            let hot_pts: PlotPoints = self
                .history
                .iter()
                .filter(|d| d.sys_time >= start_time)
                .map(|d| [d.sys_time, d.t_hot])
                .collect();
            let cold_pts: PlotPoints = self
                .history
                .iter()
                .filter(|d| d.sys_time >= start_time)
                .map(|d| [d.sys_time, d.t_cold])
                .collect();

            let shield_line = Line::new("Heat Shield", shield_pts)
                .width(2.5)
                .color(egui::Color32::ORANGE);
            let hot_line = Line::new("Hot Side", hot_pts)
                .width(2.5)
                .color(egui::Color32::RED);
            let cold_line = Line::new("Cold Side", cold_pts)
                .width(2.5)
                .color(egui::Color32::CYAN);

            let parsed_target = self.target_temp.parse::<f64>().unwrap_or(1400.0);
            let target_hline = HLine::new("Target Temp", parsed_target)
                .color(egui::Color32::MAGENTA)
                .width(2.0)
                .style(LineStyle::Dashed { length: 5.0 });

            Plot::new("telemetry_plot")
                .view_aspect(2.4)
                .legend(Legend::default().position(Corner::LeftTop))
                .set_margin_fraction(egui::Vec2::new(0.0, 0.1))
                .include_x(start_time)
                .include_x(latest_time)
                .show(ui, |plot_ui| {
                    plot_ui.hline(target_hline);
                    plot_ui.line(shield_line);
                    plot_ui.line(hot_line);
                    plot_ui.line(cold_line);
                });

            ui.add_space(15.0);

            if let Some(latest) = self.history.last() {
                let card_frame = egui::Frame::default()
                    .inner_margin(12.0)
                    .corner_radius(5.0)
                    .stroke(egui::Stroke::new(
                        1.0,
                        ui.visuals().widgets.noninteractive.bg_stroke.color,
                    ))
                    .fill(ui.visuals().widgets.noninteractive.bg_fill);

                ui.columns(4, |columns| {
                    card_frame.show(&mut columns[0], |ui| {
                        ui.vertical_centered(|ui| {
                            ui.label("Heat Shield");
                            ui.label(
                                egui::RichText::new(format!("{:.1} °C", latest.t_shield))
                                    .size(28.0)
                                    .strong()
                                    .color(egui::Color32::ORANGE),
                            );
                        });
                    });
                    card_frame.show(&mut columns[1], |ui| {
                        ui.vertical_centered(|ui| {
                            ui.label("Hot Side");
                            ui.label(
                                egui::RichText::new(format!("{:.1} °C", latest.t_hot))
                                    .size(28.0)
                                    .strong()
                                    .color(egui::Color32::RED),
                            );
                            if self.peak_hot_temp > 0.0 {
                                ui.label(
                                    egui::RichText::new(format!(
                                        "Peak: {:.1} °C",
                                        self.peak_hot_temp
                                    ))
                                    .color(egui::Color32::GRAY),
                                );
                            } else {
                                ui.label(egui::RichText::new("--").color(egui::Color32::GRAY));
                            }
                        });
                    });
                    card_frame.show(&mut columns[2], |ui| {
                        ui.vertical_centered(|ui| {
                            ui.label("Cold Side");
                            ui.label(
                                egui::RichText::new(format!("{:.1} °C", latest.t_cold))
                                    .size(28.0)
                                    .strong()
                                    .color(egui::Color32::CYAN),
                            );
                            if self.peak_cold_temp > 0.0 {
                                ui.label(
                                    egui::RichText::new(format!(
                                        "Peak: {:.1} °C",
                                        self.peak_cold_temp
                                    ))
                                    .color(egui::Color32::GRAY),
                                );
                            } else {
                                ui.label(egui::RichText::new("--").color(egui::Color32::GRAY));
                            }
                        });
                    });
                    card_frame.show(&mut columns[3], |ui| {
                        ui.vertical_centered(|ui| {
                            let duration = self.test_duration.parse::<f64>().unwrap_or(60.0);
                            let is_testing = self.current_state == SystemState::Testing;
                            let time_left = if is_testing {
                                (duration - latest.test_timer).max(0.0)
                            } else {
                                duration
                            };
                            ui.label("Time Remaining");
                            ui.label(
                                egui::RichText::new(format!("{:.1} s", time_left))
                                    .size(28.0)
                                    .strong()
                                    .color(if is_testing {
                                        egui::Color32::GREEN
                                    } else {
                                        egui::Color32::GRAY
                                    }),
                            );
                        });
                    });
                });
            }

            ui.add_space(15.0);
            ui.horizontal(|ui| {
                ui.label("Graph window:");
                ui.add(egui::TextEdit::singleline(&mut self.graph_window).desired_width(50.0));
                ui.label("seconds");
            });
        });
    }
}

impl AppState {
    fn export_csv(&self) {
        if let Some(path) = rfd::FileDialog::new()
            .set_file_name(&format!("{}.csv", self.test_name))
            .add_filter("CSV", &["csv"])
            .save_file()
        {
            let mut wtr = csv::Writer::from_path(path).expect("Failed to create CSV");
            wtr.write_record(["Test_Time(s)", "Shield(C)", "Hot(C)", "Cold(C)"])
                .unwrap();
            for row in &self.test_history {
                let relative_time = (row.sys_time - self.test_start_offset).max(0.0);
                wtr.write_record([
                    format!("{:.2}", relative_time),
                    format!("{:.2}", row.t_shield),
                    format!("{:.2}", row.t_hot),
                    format!("{:.2}", row.t_cold),
                ])
                .unwrap();
            }
            wtr.flush().unwrap();
        }
    }
}
