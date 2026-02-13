#[derive(Default)]
pub struct Data {
    pub history: Vec<[f64; 2]>,
}

#[derive(PartialEq)]
pub enum StatusCycle {
    Idle,
    Starting,
    Running,
    Stopping,
}
#[derive(PartialEq)]
pub enum StatusHeat {
    Heating,
    Hot,
    Cooling,
    Cold,
}

pub struct ModelState {
    pub target_temperature: u32,
    pub target_time: u32,
    pub status_cycle: StatusCycle,
    pub status_heat: StatusHeat,
    pub data: Data,
}

impl ModelState {
    pub fn new() -> Self {
        Self {
            target_temperature: 0,
            target_time: 0,
            status_cycle: StatusCycle::Idle,
            status_heat: StatusHeat::Cold,
            data: Data::default(),
        }
    }
}
