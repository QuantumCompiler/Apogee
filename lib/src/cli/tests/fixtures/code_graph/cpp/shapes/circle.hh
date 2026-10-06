#pragma once

#include "shape.hh"

namespace geo {

class Circle : public Shape {
public:
    explicit Circle(double r);
    double area() const override;

private:
    double radius_;
};

}  // namespace geo
