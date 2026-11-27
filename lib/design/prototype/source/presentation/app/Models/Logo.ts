import logo from '@assets/Logo-320.png'

/**
 * The logo the gallery's Foundations page draws (design 1q):
 * `lib/design/assets/Logo-320.png`, made from `Logo.png` by the Mac app's
 * `icon.sh` (the Apple apps' 11a), inlined by the build as a `data:` URL
 * so the built file stays one file. `app` reads it and hands it to the
 * gallery, which imports `styles` alone.
 */
export const logoArtwork: string = logo
