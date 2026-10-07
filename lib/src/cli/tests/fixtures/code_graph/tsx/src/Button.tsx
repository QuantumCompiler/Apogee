import React from 'react';
import { report } from './report';

type Props = { label: string };

export function Button(props: Props) {
  return <button onClick={() => report(props.label)}>{props.label}</button>;
}

export const Toolbar = () => (
  <div>
    <Button label="save" />
  </div>
);
