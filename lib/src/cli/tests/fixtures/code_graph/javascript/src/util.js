export function format(value) {
  return `[${value}]`;
}

export class Logger {
  log(message) {
    console.log(format(message));
  }
}
