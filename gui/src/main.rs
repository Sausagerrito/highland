/* ---------------------------------------------------------------------------- */
/* IMPORTS */
/* ---------------------------------------------------------------------------- */
use crossbeam_channel::{Receiver, Sender, unbounded};
use eframe::egui;
use egui_plot::{Corner, Legend, Line, LineStyle, Plot};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::thread;
use std::time::Duration;

/* ---------------------------------------------------------------------------- */
/* MESSAGING & ENUMS */
/* ---------------------------------------------------------------------------- */
#[derive(Clone, Debug)]
enum AppCommand {
    Home,
    Start,
    Stop,
    Purge,
    SetCurve([f64; 5]),
    SetTime(f64),
    SetGas(f64),
    SetSprk(f64),
    SetAirOn(f64),
    SetAirOff(f64),
}

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
    target_temp: f64,
    output: f64,
    state: SystemState,
}

/* ---------------------------------------------------------------------------- */
/* CONFIGURATION & STATE */
/* ---------------------------------------------------------------------------- */
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
struct AppConfig {
    export_directory: PathBuf,
    curve_points: [f64; 5],
    graph_window_sec: f64,
    gas_sec: f64,
    sprk_sec: f64,
    air_on_sec: f64,
    air_off_sec: f64,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            export_directory: std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")),
            curve_points: [1200.0; 5],
            graph_window_sec: 60.0,
            gas_sec: 5.0,
            sprk_sec: 2.0,
            air_on_sec: 1.0,
            air_off_sec: 1.0,
        }
    }
}

struct Toast {
    message: String,
    timer: f64,
    is_error: bool,
}

struct AppState {
    config: AppConfig,
    test_name: String,
    test_duration_sec: f64,
    is_recording: bool,
    awaiting_ready: bool,
    test_start_offset: f64,
    current_state: SystemState,
    peak_hot_temp: f64,
    peak_cold_temp: f64,
    toast: Option<Toast>,
    history: Vec<Telemetry>,
    test_history: Vec<Telemetry>,
    tx_cmd: Sender<AppCommand>,
    rx_telemetry: Receiver<Telemetry>,
}

/* ---------------------------------------------------------------------------- */
/* LOGIC & METHODS */
/* ---------------------------------------------------------------------------- */
impl AppState {
    fn new(
        cc: &eframe::CreationContext<'_>,
        tx_cmd: Sender<AppCommand>,
        rx_telemetry: Receiver<Telemetry>,
    ) -> Self {
        let config: AppConfig = if let Some(storage) = cc.storage {
            eframe::get_value(storage, eframe::APP_KEY).unwrap_or_default()
        } else {
            AppConfig::default()
        };

        let mut test_name = "UL2596_Test_01".to_owned();
        let mut path = config.export_directory.join(format!("{}.csv", test_name));

        while path.exists() {
            test_name = Self::bump_name(&test_name);
            path = config.export_directory.join(format!("{}.csv", test_name));
        }

        Self {
            config,
            test_name,
            test_duration_sec: 600.0,
            is_recording: false,
            awaiting_ready: false,
            test_start_offset: 0.0,
            current_state: SystemState::Idle,
            peak_hot_temp: 0.0,
            peak_cold_temp: 0.0,
            toast: None,
            history: Vec::with_capacity(1000),
            test_history: Vec::with_capacity(80000),
            tx_cmd,
            rx_telemetry,
        }
    }

    fn bump_name(name: &str) -> String {
        if let Some(pos) = name.rfind('_') {
            let suffix = &name[pos + 1..];
            if let Ok(num) = suffix.parse::<u32>() {
                return format!("{}_{:02}", &name[..pos], num + 1);
            }
        }
        format!("{}_01", name)
    }

    fn show_toast(&mut self, message: impl Into<String>, is_error: bool) {
        self.toast = Some(Toast {
            message: message.into(),
            timer: 5.0,
            is_error,
        });
    }

    fn auto_export_csv(&mut self) {
        if self.test_history.is_empty() {
            return;
        }

        let mut name_to_save = self.test_name.clone();
        let mut path = self
            .config
            .export_directory
            .join(format!("{}.csv", name_to_save));

        while path.exists() {
            name_to_save = Self::bump_name(&name_to_save);
            path = self
                .config
                .export_directory
                .join(format!("{}.csv", name_to_save));
        }

        match csv::Writer::from_path(&path) {
            Ok(mut wtr) => {
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
                self.show_toast(format!("Auto-saved: {}.csv", name_to_save), false);
            }
            Err(e) => {
                self.show_toast(format!("Auto-save failed: {}", e), true);
            }
        }

        self.test_name = Self::bump_name(&name_to_save);
    }

    fn export_csv(&mut self) {
        if let Some(path) = rfd::FileDialog::new()
            .set_file_name(&format!("{}.csv", self.test_name))
            .add_filter("CSV", &["csv"])
            .save_file()
        {
            match csv::Writer::from_path(&path) {
                Ok(mut wtr) => {
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
                    let file_name = path.file_name().unwrap_or_default().to_string_lossy();
                    self.show_toast(format!("Exported to: {}", file_name), false);
                }
                Err(e) => {
                    self.show_toast(format!("Export failed: {}", e), true);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------------- */
/* THREAD SPAWNING & RUNTIME */
/* ---------------------------------------------------------------------------- */

fn autodetect_teensy_port() -> Option<String> {
    let ports = serialport::available_ports().ok()?;

    for port in ports {
        if let serialport::SerialPortType::UsbPort(info) = port.port_type {
            if info.vid == 0x16C0 || info.vid == 0x2341 || info.vid == 0x1A86 {
                return Some(port.port_name);
            }
        }
    }

    None
}

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1100.0, 800.0]),
        ..Default::default()
    };

    let (tx_cmd, rx_cmd) = unbounded::<AppCommand>();
    let (tx_telemetry, rx_telemetry) = unbounded::<Telemetry>();

    thread::spawn(move || {
        // NOTE: For Arduino Uno simulation testing, you might need to change
        // `autodetect_teensy_port()` to explicitly return your Uno's COM port,
        // e.g., `Some("COM3".to_string())` or update the VID match in the function above.
        let port_name = autodetect_teensy_port().expect("Failed to find Teensy. Is it plugged in?");
        let baud_rate = 115200;

        println!("✅ Teensy automatically detected on port: {}", port_name);

        let mut port = serialport::new(port_name, baud_rate)
            .timeout(Duration::from_millis(10))
            .open()
            .expect("Failed to open serial port.");

        let mut serial_buf: Vec<u8> = vec![0; 1000];
        let mut line_buffer = String::new();

        loop {
            /* -------------------------------------------------------- */
            /* 1. SEND COMMANDS TO TEENSY */
            /* -------------------------------------------------------- */
            while let Ok(cmd) = rx_cmd.try_recv() {
                let msg = match cmd {
                    AppCommand::Home => "CMD:HOME\n".to_string(),
                    AppCommand::Start => "CMD:START\n".to_string(),
                    AppCommand::Stop => "CMD:STOP\n".to_string(),
                    AppCommand::Purge => "CMD:PURGE\n".to_string(),
                    AppCommand::SetTime(time) => format!("SET_TIME:{}\n", time),
                    AppCommand::SetCurve(c) => format!(
                        "SET_CURVE:{:.1},{:.1},{:.1},{:.1},{:.1}\n",
                        c[0], c[1], c[2], c[3], c[4]
                    ),
                    AppCommand::SetGas(s) => format!("SET_GAS:{}\n", (s * 1000.0) as u32),
                    AppCommand::SetSprk(s) => format!("SET_SPRK:{}\n", (s * 1000.0) as u32),
                    AppCommand::SetAirOn(s) => format!("AIR_ON:{}\n", (s * 1000.0) as u32),
                    AppCommand::SetAirOff(s) => format!("AIR_OFF:{}\n", (s * 1000.0) as u32),
                };
                let _ = port.write(msg.as_bytes());
            }

            /* -------------------------------------------------------- */
            /* 2. READ TELEMETRY FROM TEENSY */
            /* -------------------------------------------------------- */
            match port.read(serial_buf.as_mut_slice()) {
                Ok(t) => {
                    if let Ok(s) = std::str::from_utf8(&serial_buf[..t]) {
                        line_buffer.push_str(s);
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => (),
                Err(e) => eprintln!("Serial Read Error: {:?}", e),
            }

            while let Some(pos) = line_buffer.find('\n') {
                let line = line_buffer[..pos].trim().to_string();
                line_buffer.drain(..=pos);

                if line.starts_with("SYS_TIME:") {
                    if let Some(telemetry) = parse_telemetry(&line) {
                        let _ = tx_telemetry.send(telemetry);
                    }
                } else {
                    println!("Teensy: {}", line);
                }
            }

            thread::sleep(Duration::from_millis(10));
        }
    });

    eframe::run_native(
        "UL2596 TAG Test Controller",
        options,
        Box::new(move |cc| {
            // Force dark mode context globally
            cc.egui_ctx.set_visuals(egui::Visuals::dark());

            setup_custom_fonts(&cc.egui_ctx);
            egui_extras::install_image_loaders(&cc.egui_ctx);
            Ok(Box::new(AppState::new(cc, tx_cmd, rx_telemetry)))
        }),
    )
}

fn parse_telemetry(line: &str) -> Option<Telemetry> {
    let mut sys_time = 0.0;
    let mut test_timer = 0.0;
    let mut t_shield = 0.0;
    let mut t_hot = 0.0;
    let mut t_cold = 0.0;
    let mut target_temp = 0.0;
    let mut output = 0.0;
    let mut state = SystemState::Idle;

    let parts: Vec<&str> = line.split_whitespace().collect();

    for part in parts {
        let mut kv = part.split(':');
        if let (Some(key), Some(value)) = (kv.next(), kv.next()) {
            match key {
                "SYS_TIME" => sys_time = value.parse().unwrap_or(0.0),
                "TEST_TIMER" => test_timer = value.parse().unwrap_or(0.0),
                "T_SHIELD" => t_shield = value.parse().unwrap_or(0.0),
                "T_HOT" => t_hot = value.parse().unwrap_or(0.0),
                "T_COLD" => t_cold = value.parse().unwrap_or(0.0),
                "TARGET" => target_temp = value.parse().unwrap_or(0.0),
                "OUTPUT" => output = value.parse().unwrap_or(0.0),
                "STATE" => {
                    state = match value {
                        "HOMING" => SystemState::Homing,
                        "READY" => SystemState::Ready,
                        "HEATING" => SystemState::Heating,
                        "TESTING" => SystemState::Testing,
                        _ => SystemState::Idle,
                    }
                }
                _ => {}
            }
        }
    }

    Some(Telemetry {
        sys_time,
        test_timer,
        t_shield,
        t_hot,
        t_cold,
        target_temp,
        output,
        state,
    })
}

fn setup_custom_fonts(ctx: &egui::Context) {
    let mut style = (*ctx.global_style()).clone();

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
        std::sync::Arc::new(egui::FontData::from_static(include_bytes!(
            "../assets/JetBrainsMonoNerdFont-SemiBold.ttf"
        ))),
    );
    fonts
        .families
        .get_mut(&egui::FontFamily::Proportional)
        .unwrap()
        .insert(0, "ui_font".to_owned());
    ctx.set_fonts(fonts);
}

/* ---------------------------------------------------------------------------- */
/* EFRAME EVENT LOOP & UI */
/* ---------------------------------------------------------------------------- */
impl eframe::App for AppState {
    fn save(&mut self, storage: &mut dyn eframe::Storage) {
        eframe::set_value(storage, eframe::APP_KEY, &self.config);
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.process_telemetry();

        // Used egui::SidePanel here instead of Panel and wrapped contents in a ScrollArea
        egui::SidePanel::left("control_panel")
            .resizable(true)
            .max_size(300.0)
            .show_inside(ui, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    self.draw_side_panel(ui);
                });
            });

        // Wrapped CentralPanel contents in a ScrollArea
        egui::CentralPanel::default().show_inside(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                self.draw_dashboard(ui);
            });
        });

        self.draw_toast(ui.ctx());
        ui.ctx().request_repaint();
    }
}

/* ---------------------------------------------------------------------------- */
/* UI COMPONENTS */
/* ---------------------------------------------------------------------------- */
impl AppState {
    fn process_telemetry(&mut self) {
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
                let _ = self.tx_cmd.send(AppCommand::Start);
            }

            if self.current_state == SystemState::Testing {
                self.peak_hot_temp = self.peak_hot_temp.max(data.t_hot);
                self.peak_cold_temp = self.peak_cold_temp.max(data.t_cold);
            }

            self.history.push(data.clone());
            if self.is_recording {
                self.test_history.push(data.clone());
            }
        }
    }

    fn draw_side_panel(&mut self, ui: &mut egui::Ui) {
        ui.add_space(10.0);

        /* Config Frame */
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.heading(egui::RichText::new("Configuration").strong().size(18.0));
            ui.add_space(8.0);

            ui.label("Test Name:");
            ui.text_edit_singleline(&mut self.test_name);
            ui.add_space(8.0);

            egui::Grid::new("timing_grid")
                .num_columns(2)
                .spacing([20.0, 6.0])
                .show(ui, |ui| {
                    ui.label("Test Duration:");
                    ui.add(
                        egui::DragValue::new(&mut self.test_duration_sec)
                            .speed(10.0)
                            .range(10.0..=3600.0),
                    );
                    ui.end_row();

                    ui.label("Gas Preflow:");
                    ui.add(
                        egui::DragValue::new(&mut self.config.gas_sec)
                            .speed(0.1)
                            .range(0.1..=60.0),
                    );
                    ui.end_row();

                    ui.label("Spark Ignition:");
                    ui.add(
                        egui::DragValue::new(&mut self.config.sprk_sec)
                            .speed(0.1)
                            .range(0.1..=60.0),
                    );
                    ui.end_row();

                    ui.label("Compressed Air ON:");
                    ui.add(
                        egui::DragValue::new(&mut self.config.air_on_sec)
                            .speed(0.1)
                            .range(0.1..=60.0),
                    );
                    ui.end_row();

                    ui.label("Compressed Air OFF:");
                    ui.add(
                        egui::DragValue::new(&mut self.config.air_off_sec)
                            .speed(0.1)
                            .range(0.1..=60.0),
                    );
                    ui.end_row();
                });

            ui.add_space(20.0);

            ui.label(
                egui::RichText::new("Target Temp Curve (°C):")
                    .color(egui::Color32::MAGENTA)
                    .strong(),
            );
            ui.add_space(8.0);

            ui.scope(|ui| {
                ui.spacing_mut().slider_width = 200.0;
                ui.spacing_mut().item_spacing = egui::vec2(4.0, 4.0);

                ui.columns(5, |columns| {
                    for i in 0..5 {
                        columns[i].vertical_centered(|ui| {
                            ui.add(
                                egui::Slider::new(&mut self.config.curve_points[i], 100.0..=1200.0)
                                    .vertical()
                                    .show_value(false)
                                    .step_by(10.0),
                            );
                            ui.add_space(2.0);
                            ui.add(
                                egui::DragValue::new(&mut self.config.curve_points[i]).speed(5.0),
                            );
                            ui.label(
                                egui::RichText::new(format!("{}%", i * 25))
                                    .small()
                                    .color(egui::Color32::GRAY),
                            );
                        });
                    }
                });
            });
        });

        ui.add_space(20.0);

        /* Execution Frame */
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.heading(egui::RichText::new("Execution").strong().size(18.0));
            ui.add_space(8.0);

            ui.add_enabled_ui(
                !self.is_recording && self.current_state == SystemState::Idle,
                |ui| {
                    if ui
                        .add_sized(
                            [ui.available_width(), 35.0],
                            egui::Button::new("💨 PURGE VALVES (30s)"),
                        )
                        .clicked()
                    {
                        let _ = self.tx_cmd.send(AppCommand::Purge);
                        self.show_toast("Purge sequence initiated", false);
                    }
                },
            );

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
                    self.test_start_offset = self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
                    self.peak_hot_temp = 0.0;
                    self.peak_cold_temp = 0.0;

                    let _ = self
                        .tx_cmd
                        .send(AppCommand::SetTime(self.test_duration_sec));
                    let _ = self
                        .tx_cmd
                        .send(AppCommand::SetCurve(self.config.curve_points));

                    let _ = self.tx_cmd.send(AppCommand::SetGas(self.config.gas_sec));
                    let _ = self.tx_cmd.send(AppCommand::SetSprk(self.config.sprk_sec));
                    let _ = self
                        .tx_cmd
                        .send(AppCommand::SetAirOn(self.config.air_on_sec));
                    let _ = self
                        .tx_cmd
                        .send(AppCommand::SetAirOff(self.config.air_off_sec));

                    self.awaiting_ready = true;
                    let _ = self.tx_cmd.send(AppCommand::Home);
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
                    let _ = self.tx_cmd.send(AppCommand::Stop);
                }
            }

            ui.add_space(15.0);

            ui.add_enabled_ui(!self.is_recording, |ui| {
                ui.label(egui::RichText::new("Export Directory:").color(egui::Color32::GRAY));
                ui.add(
                    egui::Label::new(
                        egui::RichText::new(self.config.export_directory.display().to_string())
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
                        self.config.export_directory = path;
                    }
                }
            });

            ui.add_space(8.0);

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
    }

    fn draw_dashboard(&mut self, ui: &mut egui::Ui) {
        ui.add_space(10.0);

        /* Header */
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

        /* Graph */
        let latest_time = self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
        let start_time = latest_time - self.config.graph_window_sec;

        let mut shield_pts = vec![];
        let mut hot_pts = vec![];
        let mut cold_pts = vec![];
        let mut target_pts = vec![];

        for d in self.history.iter().filter(|d| d.sys_time >= start_time) {
            shield_pts.push([d.sys_time, d.t_shield]);
            hot_pts.push([d.sys_time, d.t_hot]);
            cold_pts.push([d.sys_time, d.t_cold]);
            target_pts.push([d.sys_time, d.target_temp]);
        }

        let shield_line = Line::new("Heat Shield", shield_pts)
            .width(2.5)
            .color(egui::Color32::ORANGE);
        let hot_line = Line::new("Hot Side", hot_pts)
            .width(2.5)
            .color(egui::Color32::RED);
        let cold_line = Line::new("Cold Side", cold_pts)
            .width(2.5)
            .color(egui::Color32::CYAN);
        let target_line = Line::new("Target Temp", target_pts)
            .width(2.0)
            .color(egui::Color32::MAGENTA)
            .style(LineStyle::Dashed { length: 5.0 });

        Plot::new("telemetry_plot")
            .view_aspect(2.4)
            .legend(Legend::default().position(Corner::LeftTop))
            .set_margin_fraction(egui::Vec2::new(0.0, 0.1))
            .label_formatter(|_, value| format!("Time: {:.1} s\nTemp: {:.1} °C", value.x, value.y))
            .include_x(start_time)
            .include_x(latest_time)
            .show(ui, |plot_ui| {
                plot_ui.line(target_line);
                plot_ui.line(shield_line);
                plot_ui.line(hot_line);
                plot_ui.line(cold_line);
            });

        ui.add_space(15.0);

        /* Stat Cards */
        if let Some(latest) = self.history.last() {
            let card_frame = egui::Frame::default()
                .inner_margin(12)
                .corner_radius(5.0)
                .stroke(egui::Stroke::new(
                    1.0,
                    ui.visuals().widgets.noninteractive.bg_stroke.color,
                ))
                .fill(ui.visuals().widgets.noninteractive.bg_fill);

            ui.columns(5, |columns| {
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
                                egui::RichText::new(format!("Peak: {:.1} °C", self.peak_hot_temp))
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
                                egui::RichText::new(format!("Peak: {:.1} °C", self.peak_cold_temp))
                                    .color(egui::Color32::GRAY),
                            );
                        } else {
                            ui.label(egui::RichText::new("--").color(egui::Color32::GRAY));
                        }
                    });
                });
                card_frame.show(&mut columns[3], |ui| {
                    ui.vertical_centered(|ui| {
                        let is_testing = self.current_state == SystemState::Testing;
                        let time_left = if is_testing {
                            (self.test_duration_sec - latest.test_timer).max(0.0)
                        } else {
                            self.test_duration_sec
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
                card_frame.show(&mut columns[4], |ui| {
                    ui.vertical_centered(|ui| {
                        ui.label("Valve Output");
                        let percent = (latest.output / 255.0) * 100.0;
                        ui.label(
                            egui::RichText::new(format!("{:.0}%", percent))
                                .size(28.0)
                                .strong()
                                .color(egui::Color32::LIGHT_RED),
                        );
                    });
                });
            });
        }

        ui.add_space(15.0);
        ui.horizontal(|ui| {
            ui.label("Graph window:");
            ui.add(
                egui::DragValue::new(&mut self.config.graph_window_sec)
                    .suffix(" seconds")
                    .range(10.0..=600.0),
            );
        });
    }

    fn draw_toast(&mut self, ctx: &egui::Context) {
        if let Some(toast) = &mut self.toast {
            if toast.timer > 0.0 {
                toast.timer -= ctx.input(|i| i.stable_dt) as f64;

                let icon = if toast.is_error { "❌" } else { "✅" };
                let color = if toast.is_error {
                    egui::Color32::RED
                } else {
                    egui::Color32::GREEN
                };

                egui::Window::new("App_Toast")
                    .title_bar(false)
                    .resizable(false)
                    .collapsible(false)
                    .interactable(false)
                    .anchor(egui::Align2::RIGHT_BOTTOM, egui::vec2(-20.0, -20.0))
                    .frame(
                        egui::Frame::popup(ctx.global_style().as_ref())
                            .inner_margin(12)
                            .shadow(egui::epaint::Shadow::NONE),
                    )
                    .show(ctx, |ui| {
                        ui.horizontal(|ui| {
                            ui.label(egui::RichText::new(icon).color(color).size(16.0));
                            ui.label(&toast.message);
                        });
                    });
            } else {
                self.toast = None;
            }
        }
    }
}
