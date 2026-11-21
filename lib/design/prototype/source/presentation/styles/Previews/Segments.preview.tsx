import { Segments } from '../Components/Segments'

/** Three choices, the middle one pressed, as Settings › Appearance's text size draws them. */
export const Chosen = () => <Segments label="Text size" options={[['s', 'Small'], ['m', 'Medium'], ['l', 'Large']]} current="m" pick={() => undefined} />

/** Two, as a block rather than inline (the old Thinking control). */
export const Block = () => <Segments label="New sessions are" options={[['saved', 'Saved'], ['private', 'Private']]} current="saved" pick={() => undefined} inline={false} />
