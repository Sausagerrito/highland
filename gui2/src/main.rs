// ----------------------------------------------------------------------------
// IMPORTS
// ----------------------------------------------------------------------------
use crossbeam_channel::{Receiver, Sender, unbounded};
use eframe::egui;
use egui_plot::{Corner, Legend, Line, Plot};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::thread;
use std::time::{Duration, Instant};

// ----------------------------------------------------------------------------
// MESSAGING & ENUMS
// ----------------------------------------------------------------------------
#[derive(Clone, Debug)]
enum AppCommand {
    MoveLeft,
    MoveRight,
    Start,
}

#[derive(Clone, Debug)]
struct Telemetry {
    sys_time: f64,
    t_shield: f64,
    t_hot: f64,
    t_cold: f64,
}

// ----------------------------------------------------------------------------
// CONFIGURATION & STATE
// ----------------------------------------------------------------------------
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
struct AppConfig {
    export_directory: PathBuf,
    graph_window_sec: f64,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            export_directory: std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")),
            graph_window_sec: 60.0,
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
    is_recording: bool,
    test_start_offset: f64,
    toast: Option<Toast>,
    history: Vec<Telemetry>,
    test_history: Vec<Telemetry>,
    tx_cmd: Sender<AppCommand>,
    rx_telemetry: Receiver<Telemetry>,
}

// ----------------------------------------------------------------------------
// LOGIC & METHODS
// ----------------------------------------------------------------------------
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

        let mut test_name = "Hardware_Test_01".to_owned();
        let mut path = config.export_directory.join(format!("{}.csv", test_name));

        while path.exists() {
            test_name = Self::bump_name(&test_name);
            path = config.export_directory.join(format!("{}.csv", test_name));
        }

        Self {
            config,
            test_name,
            is_recording: false,
            test_start_offset: 0.0,
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
                    self.test_name = Self::bump_name(&self.test_name);
                }
                Err(e) => {
                    self.show_toast(format!("Export failed: {}", e), true);
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// THREAD SPAWNING & RUNTIME
// ----------------------------------------------------------------------------
fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1100.0, 800.0]),
        ..Default::default()
    };

    let (tx_cmd, rx_cmd) = unbounded::<AppCommand>();
    let (tx_telemetry, rx_telemetry) = unbounded::<Telemetry>();

    thread::spawn(move || {
        let port_name = "/dev/cu.usbmodem190622201";
        let baud_rate = 115200;

        let mut port = serialport::new(port_name, baud_rate)
            .timeout(Duration::from_millis(10))
            .open()
            .expect("Failed to open serial port. Is the Teensy plugged in and the port correct?");

        let mut serial_buf: Vec<u8> = vec![0; 1000];
        let mut line_buffer = String::new();

        // Start the stopwatch for the X-Axis graph time
        let app_start_time = Instant::now();

        loop {
            // 1. SEND COMMANDS TO TEENSY
            while let Ok(cmd) = rx_cmd.try_recv() {
                let msg = match cmd {
                    AppCommand::MoveLeft => "CMD:HOME\n".to_string(),
                    AppCommand::MoveRight => "CMD:SHIELD\n".to_string(),
                    AppCommand::Start => "CMD:START\n".to_string(),
                };
                let _ = port.write(msg.as_bytes());
            }

            // 2. READ TELEMETRY FROM TEENSY
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

                // Look for either the short "SHLD:" or the long "T_SHIELD:" format
                if line.contains("SHLD:") || line.contains("T_SHIELD:") {
                    let mut telemetry = parse_telemetry(&line);
                    // Stamp the current time onto the data
                    telemetry.sys_time = app_start_time.elapsed().as_secs_f64();
                    let _ = tx_telemetry.send(telemetry);
                } else {
                    println!("Teensy: {}", line);
                }
            }

            thread::sleep(Duration::from_millis(10));
        }
    });

    eframe::run_native(
        "UL2596 Manual Hardware Tester",
        options,
        Box::new(move |cc| {
            setup_custom_fonts(&cc.egui_ctx);
            egui_extras::install_image_loaders(&cc.egui_ctx);
            Ok(Box::new(AppState::new(cc, tx_cmd, rx_telemetry)))
        }),
    )
}

fn parse_telemetry(line: &str) -> Telemetry {
    let mut t_shield = 0.0;
    let mut t_hot = 0.0;
    let mut t_cold = 0.0;

    let parts: Vec<&str> = line.split_whitespace().collect();

    for part in parts {
        let mut kv = part.split(':');
        if let (Some(key), Some(value)) = (kv.next(), kv.next()) {
            match key {
                // Supports both formatting styles
                "SHLD" | "T_SHIELD" => t_shield = value.parse().unwrap_or(0.0),
                "HOT" | "T_HOT" => t_hot = value.parse().unwrap_or(0.0),
                "COLD" | "T_COLD" => t_cold = value.parse().unwrap_or(0.0),
                _ => {}
            }
        }
    }

    Telemetry {
        sys_time: 0.0, // This gets overwritten by the thread timer anyway
        t_shield,
        t_hot,
        t_cold,
    }
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

// ----------------------------------------------------------------------------
// EFRAME EVENT LOOP & UI
// ----------------------------------------------------------------------------
impl eframe::App for AppState {
    fn save(&mut self, storage: &mut dyn eframe::Storage) {
        eframe::set_value(storage, eframe::APP_KEY, &self.config);
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.process_telemetry();

        egui::Panel::left("control_panel")
            .resizable(true)
            .max_size(300.0)
            .show_inside(ui, |ui| self.draw_side_panel(ui));

        egui::CentralPanel::default().show_inside(ui, |ui| self.draw_dashboard(ui));

        self.draw_toast(ui.ctx());
        ui.ctx().request_repaint();
    }
}

// ----------------------------------------------------------------------------
// UI COMPONENT METHODS
// ----------------------------------------------------------------------------
impl AppState {
    fn process_telemetry(&mut self) {
        while let Ok(data) = self.rx_telemetry.try_recv() {
            self.history.push(data.clone());
            if self.is_recording {
                self.test_history.push(data.clone());
            }
        }
    }

    fn draw_side_panel(&mut self, ui: &mut egui::Ui) {
        ui.add_space(10.0);

        // Settings
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.heading(egui::RichText::new("Data Setup").strong().size(18.0));
            ui.add_space(8.0);

            ui.label("Test Name:");
            ui.text_edit_singleline(&mut self.test_name);
            ui.add_space(8.0);
        });

        ui.add_space(20.0);

        // Hardware Controls
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.heading(egui::RichText::new("Hardware Control").strong().size(18.0));
            ui.add_space(8.0);

            if ui
                .add_sized(
                    [ui.available_width(), 40.0],
                    egui::Button::new("◀ MOVE LEFT (HOME)"),
                )
                .clicked()
            {
                let _ = self.tx_cmd.send(AppCommand::MoveLeft);
            }

            ui.add_space(8.0);

            if ui
                .add_sized(
                    [ui.available_width(), 40.0],
                    egui::Button::new("MOVE RIGHT (SHIELD) ▶"),
                )
                .clicked()
            {
                let _ = self.tx_cmd.send(AppCommand::MoveRight);
            }

            ui.add_space(15.0);
            ui.separator();
            ui.add_space(15.0);

            let start_btn_text = if self.is_recording {
                "🔥 TEST IN PROGRESS..."
            } else {
                "🔥 START TEST"
            };
            let start_btn_color = if self.is_recording {
                egui::Color32::ORANGE
            } else {
                egui::Color32::GREEN
            };

            if ui
                .add_sized(
                    [ui.available_width(), 50.0],
                    egui::Button::new(
                        egui::RichText::new(start_btn_text)
                            .strong()
                            .color(start_btn_color),
                    ),
                )
                .clicked()
                && !self.is_recording
            {
                self.is_recording = true;
                self.test_history.clear();
                self.test_start_offset = self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
                let _ = self.tx_cmd.send(AppCommand::Start);
            }
        });

        ui.add_space(20.0);

        // Save Controls
        egui::Frame::group(ui.style()).show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.heading(egui::RichText::new("Data Export").strong().size(18.0));
            ui.add_space(8.0);

            ui.add_enabled_ui(self.is_recording || !self.test_history.is_empty(), |ui| {
                if ui
                    .add_sized(
                        [ui.available_width(), 45.0],
                        egui::Button::new(
                            egui::RichText::new("💾 SAVE & STOP")
                                .strong()
                                .color(egui::Color32::LIGHT_BLUE),
                        ),
                    )
                    .clicked()
                {
                    self.is_recording = false;
                    self.export_csv();
                }
            });
        });
    }

    fn draw_dashboard(&mut self, ui: &mut egui::Ui) {
        ui.add_space(10.0);

        // Header
        ui.horizontal(|ui| {
            let heading_height = ui.text_style_height(&egui::TextStyle::Heading);
            ui.add(
                egui::Image::new(egui::include_image!("../assets/logo.png"))
                    .fit_to_exact_size(egui::vec2(heading_height, heading_height)),
            );
            ui.add_space(4.0);
            ui.heading(egui::RichText::new("MANUAL HARDWARE TESTER").strong());

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let (color, text) = if self.is_recording {
                    (egui::Color32::RED, "⏺ RECORDING DATA")
                } else {
                    (egui::Color32::DARK_GRAY, "⏸ STANDBY")
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
            });
        });

        ui.separator();
        ui.add_space(10.0);

        // Graph
        let latest_time = self.history.last().map(|d| d.sys_time).unwrap_or(0.0);
        let start_time = latest_time - self.config.graph_window_sec;

        let mut shield_pts = vec![];
        let mut hot_pts = vec![];
        let mut cold_pts = vec![];

        for d in self.history.iter().filter(|d| d.sys_time >= start_time) {
            shield_pts.push([d.sys_time, d.t_shield]);
            hot_pts.push([d.sys_time, d.t_hot]);
            cold_pts.push([d.sys_time, d.t_cold]);
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

        Plot::new("telemetry_plot")
            .view_aspect(2.4)
            .legend(Legend::default().position(Corner::LeftTop))
            .set_margin_fraction(egui::Vec2::new(0.0, 0.1))
            .label_formatter(|_, value| format!("Time: {:.1} s\nTemp: {:.1} °C", value.x, value.y))
            .include_x(start_time)
            .include_x(latest_time)
            .show(ui, |plot_ui| {
                plot_ui.line(shield_line);
                plot_ui.line(hot_line);
                plot_ui.line(cold_line);
            });

        ui.add_space(15.0);

        // Stat Cards
        if let Some(latest) = self.history.last() {
            let card_frame = egui::Frame::default()
                .inner_margin(12)
                .corner_radius(5.0)
                .stroke(egui::Stroke::new(
                    1.0,
                    ui.visuals().widgets.noninteractive.bg_stroke.color,
                ))
                .fill(ui.visuals().widgets.noninteractive.bg_fill);

            ui.columns(3, |columns| {
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
