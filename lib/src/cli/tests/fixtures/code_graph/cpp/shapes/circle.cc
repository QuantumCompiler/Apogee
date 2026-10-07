#include "circle.hh"

#include <cmath>

namespace geo {
namespace {

double square(double x) {
    return x * x;
}

}  // namespace

Circle::Circle(double r) : radius_{r} {}

double Circle::area() const {
    return M_PI * square(radius_);
}

double total_area(const Shape& a, const Shape& b) {
    return a.area() + b.area();
}

std::string Shape::name() const {
    return "shape";
}

}  // namespace geo
