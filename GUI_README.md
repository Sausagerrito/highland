# The GUI

This will be a simple app that displays data, and ways to control the system. It will also handle the conversion of raw data into spreadsheets and graphs.

It is written in Rust, and uses EFRAME to render which should be super lightweight, responsive and safe.

The communication protocol between the MCU and app will have to be EXACTLY the same and thoroughly tested. There will need to be hardcoded temperature and time limits, and fault handling.
