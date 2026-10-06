import { format } from './util.js';

export function Label({ text }) {
  return <span className="label">{format(text)}</span>;
}

export const Panel = () => <Label text="ready" />;
