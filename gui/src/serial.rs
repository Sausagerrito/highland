use std::io::{BufRead, BufReader, Write, stdin, stdout};
use std::sync::mpsc::{Receiver, Sender, channel};
use std::thread::spawn;
use std::time::{Duration, Instant};

pub fn start_serial_worker(port_path: &str) -> (Sender<String>, Receiver<(f64, f64)>) {
    let (tx_data, rx_data) = channel::<(f64, f64)>();
    let (tx_cmd, rx_cmd) = channel::<String>();
    let path = port_path.to_string();

    spawn(move || {
        println!("Opening port: {path}.");
        let Ok(port) = serialport::new(path, 9600)
            .timeout(Duration::from_millis(100))
            .open()
        else {
            println!("Could not open serial port. Thread Exiting");
            return;
        };

        println!("Cloning the port for writer & reader.");
        let mut writer = port.try_clone().unwrap();
        let mut reader = BufReader::new(port);
        let start_instant = Instant::now();

        loop {
            if let Ok(cmd) = rx_cmd.try_recv() {
                println!("Command '{cmd}' recieved.");
                writer.write_all(cmd.as_bytes()).unwrap();
            }

            let mut buffer = String::new();

            println!("Reading buffer line.");
            reader.read_line(&mut buffer).unwrap();

            if !buffer.is_empty() {
                let temperature = buffer.trim().parse::<f64>().unwrap();

                let time_elapsed = start_instant.elapsed().as_secs_f64();

                tx_data.send((time_elapsed, temperature)).unwrap();
            }
        }
    });

    (tx_cmd, rx_data)
}

pub fn get_serial_port() -> String {
    let ports: Vec<String> = serialport::available_ports()
        .unwrap()
        .into_iter()
        .map(|p| p.port_name)
        .collect();

    for (i, name) in ports.iter().enumerate() {
        println!("{}: {}", i, name);
    }

    println!("Choose port: ");
    stdout().flush().unwrap();

    let mut input = String::new();

    stdin().read_line(&mut input).unwrap();

    let port_index: usize = input.trim().parse().unwrap();

    ports[port_index].clone()
}
