#pragma once

#include <string>

namespace geo {

class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    std::string name() const;
};

double total_area(const Shape& a, const Shape& b);

}  // namespace geo
