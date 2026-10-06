export interface Shape {
  area(): number;
}

export class Square implements Shape {
  constructor(private side: number) {}

  area(): number {
    return this.side * this.side;
  }
}

export namespace Units {
  export function cm(value: number): string {
    return `${value}cm`;
  }
}
