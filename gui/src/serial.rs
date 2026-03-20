use std::io::{BufRead, BufReader, Write, stdin, stdout};
use std::sync::mpsc::{Receiver, Sender, TryRecvError, channel};
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
            match rx_cmd.try_recv() {
                Ok(cmd) => {
                    println!("Command '{cmd}' recieved.");

                    if let Err(e) = writer.write_all(cmd.as_bytes()) {
                        eprintln!("Failed to write command to destination: {e}")
                    }
                }
                Err(TryRecvError::Empty) => {}

                Err(TryRecvError::Disconnected) => {
                    println!("Channel disconnected.")
                }
            }

            let mut buffer = String::new();

            println!("Reading buffer line.");
            let read_result = reader.read_line(&mut buffer);

            match read_result {
                Ok(0) => {
                    break;
                }
                Err(e) => {
                    eprintln!("{e}");
                    break;
                }
                Ok(_) => {
                    if !buffer.is_empty() {
                        if let Ok(temperature) = buffer.trim().parse::<f64>() {
                            let time_elapsed = start_instant.elapsed().as_secs_f64();
                            tx_data.send((time_elapsed, temperature)).unwrap();
                        }
                    }
                }
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

    loop {
        println!("Choose port: ");
        stdout().flush().unwrap();

        let mut input = String::new();
        stdin().read_line(&mut input).unwrap();

        let port_index: usize = match input.trim().parse() {
            Ok(num) => num,
            Err(_) => {
                println!("Enter a valid number.");
                continue;
            }
        };

        match ports.get(port_index) {
            Some(name) => return name.clone(),
            None => println!("Index {port_index} out of range"),
        }
    }
}
