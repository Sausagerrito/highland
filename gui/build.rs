use std::io;

fn main() -> io::Result<()> {
    // Only compile the resource on Windows
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        let mut res = winres::WindowsResource::new();
        res.set_icon("icon.ico");
        res.set_windres_path("x86_64-w64-mingw32-windres");
        res.compile()?;
    }
    Ok(())
}
