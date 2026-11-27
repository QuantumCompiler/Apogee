import { useObserved } from '../../styles/Observation/useObserved'
import { SuiteDetail, SuitesScreen } from '../Views/SuitesScreen'
import { useSuitesPreview } from './SuitesPreview'

/** The place as `suites` opens it: the recorded five, the default (fast) chosen, its members holding nothing here. */
export const DefaultChosen = () => <SuitesScreen model={useSuitesPreview()} />

/** A suite that fits on a shared base, as apogee prices it. */
export const SharedFits = () => <SuitesScreen model={useSuitesPreview({ then: (suites) => suites.select('shared') })} />

/** A suite apogee says does not fit: both actions say “anyway” and carry --force. */
export const VastOver = () => <SuitesScreen model={useSuitesPreview({ then: (suites) => suites.select('vast') })} />

/** Another suite made the default, its report kept: Stop using it by default. */
export const CloudMadeDefault = () => (
  <SuitesScreen
    model={useSuitesPreview({
      then: async (suites) => {
        suites.select('cloud')
        await suites.makeDefault()
      },
    })}
  />
)

/** No session could move (a private one with a turn sent): Switch the session to it is off. */
export const NoSessionToSwitch = () => <SuitesScreen model={useSuitesPreview({ switchable: false })} />

/** No apogee: nothing read, nothing drawn, as the old place draws no missing state. */
export const Missing = () => <SuitesScreen model={useSuitesPreview({ apogee: 'missing' })} />

/** A config apogee cannot read: its refusal said in the list. */
export const ConfigTypo = () => <SuitesScreen model={useSuitesPreview({ apogee: 'config-typo' })} />

/** The chosen suite's page alone, over budget. */
export const DetailOver = () => {
  const model = useSuitesPreview({ then: (suites) => suites.select('vast') })
  useObserved(model)
  const detail = model.face().detail
  return detail.shown ? <SuiteDetail model={model} detail={detail} /> : null
}
