/**
 * The one-time install a media verb's refusal named, asked first (design
 * 1k: the old prototype's `rtc`, the Mac's `InstallConsentSheet`, promoted to
 * `styles` for Video, Compose, Voice and Settings): the CLI's own words for
 * why, what installing does, and the command -- nothing installed unless the
 * person says yes. The content the shell's sheet host frames; its title and
 * its Not now and Install buttons are `installSheetWords`', handed to the
 * host by `app`.
 */
export function InstallConsentSheet({ said, body, command }: { readonly said: string; readonly body: string; readonly command: string }) {
  return (
    <>
      <p className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>
        {said}
      </p>
      <p style={{ margin: 0, color: 'var(--text-secondary)' }}>{body}</p>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {command}
      </div>
    </>
  )
}
