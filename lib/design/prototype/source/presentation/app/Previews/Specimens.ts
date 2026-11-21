import { specimens as compose } from '../../compose/Previews/Specimens'
import { specimens as session } from '../../session/Previews/Specimens'
import { specimens as sessions } from '../../sessions/Previews/Specimens'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { specimens as styles } from '../../styles/Previews/Specimens'
import { specimens as video } from '../../video/Previews/Specimens'
import { specimens as voice } from '../../voice/Previews/Specimens'

/**
 * Every module's specimens, gathered for the gallery's Components pages
 * (design 1q): each feature module exports its own from its `Previews/`
 * (`session`, `sessions`, `video`, `compose` and `voice`, and `styles`'
 * shared pieces), and `app`, the one module that may import every place,
 * hands them on -- the gallery imports `styles` alone. A page draws its
 * modules in this order: the session's turn first, as the old boards led
 * with it, and the inspector beside it, then `styles`' shared pieces, then
 * the media places. A module the old Components boards drew nothing of
 * (Models, Training, Settings and the rest) has none.
 */
export const specimens: readonly Specimen[] = [...session, ...sessions, ...styles, ...video, ...compose, ...voice]
