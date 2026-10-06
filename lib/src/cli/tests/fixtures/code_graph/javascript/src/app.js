import { format, Logger } from './util.js';
import * as path from 'path';
const fs = require('fs');

class App extends Logger {
  run(file) {
    this.log(format(path.basename(file)));
    return fs.existsSync(file);
  }
}

const start = () => new App().run('x');
