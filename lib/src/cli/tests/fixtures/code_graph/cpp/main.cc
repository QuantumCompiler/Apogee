#include <iostream>
#include <memory>

#include "shapes/circle.hh"

using geo::Circle;

int main() {
    auto owned = std::make_unique<Circle>(2.0);
    Circle unit{1.0};
    std::cout << geo::total_area(unit, *owned) << "\n";
    std::unique_ptr<geo::Shape> shape = std::move(owned);
    return shape->area() > 0 ? 0 : 1;
}
