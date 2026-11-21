import { PickColumn, PickRow } from '../Components/PickList'

/** The Library's definitions: the person's own agent current among the shipped ones. */
export const Definitions = () => (
  <div className="ap" data-theme="dark" style={{ display: 'flex', height: 320 }}>
    <PickColumn label="Definitions">
      <PickRow name="security-review" meta="shipped · read-only" current={false} pick={() => {}} />
      <PickRow name="triage" meta="yours · read-only" current pick={() => {}} />
    </PickColumn>
  </div>
)

/** Review's skills: what each lens applies to in mono, cut to one line, security current. */
export const Skills = () => (
  <div className="ap" data-theme="dark" style={{ display: 'flex', height: 320 }}>
    <PickColumn label="Skills">
      <PickRow name="ai-ml" meta="shipped · .ipynb, **/train/**, **/training/**, **/ml/**, **/models/**" current={false} pick={() => {}} mono />
      <PickRow name="security" meta="shipped · every file" current pick={() => {}} mono />
    </PickColumn>
  </div>
)
