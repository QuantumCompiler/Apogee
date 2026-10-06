import { Square, Shape } from './shapes';
import * as shapes from './shapes';

function total(items: Shape[]): number {
  return items.reduce((sum, s) => sum + s.area(), 0);
}

export function report(size: number): string {
  const square: Square = new Square(size);
  return shapes.Units.cm(square.area() + total([square]));
}
