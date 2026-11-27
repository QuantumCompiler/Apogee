// The type of the modules the build makes of the design's own assets (design
// 1q, tooling/assets.ts): `@assets/<file>`, one file of lib/design/assets,
// whose default export is its bytes as a `data:` URL. Only `app` imports one.

declare module '@assets/*' {
  /** The asset as a `data:` URL, inlined by the build. */
  const url: string
  export default url
}
