pub mod shapes;

use crate::shapes::{Circle, Shape};

pub fn total(items: &[Box<dyn Shape>]) -> f64 {
    items.iter().map(|s| s.area()).sum()
}

pub fn unit() -> Circle {
    let c: Circle = Circle::new(1.0);
    println!("{}", c.area());
    c
}
