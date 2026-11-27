import { NewTaskSheet } from '../Views/NewTaskSheet'
import { TaskRefusedAlert } from '../Views/TaskRefusedAlert'
import { newTaskGoal, recipeFrame } from './TasksWorlds'
import { useTasksPreview } from './TasksPreview'

/** The sheet as New task… opens it: nothing written, Start off. */
export const Opened = () => <NewTaskSheet model={useTasksPreview({ then: (place) => place.openNew() })} />

/** The sheet as `newtask` fills it: the old goal, both grants given. */
export const GoalWritten = () => (
  <NewTaskSheet
    model={useTasksPreview({
      then: async (place) => {
        await place.openNew()
        place.setForm({ goal: newTaskGoal, writes: 'allow', cmds: 'allow' })
      },
    })}
  />
)

/** A recipe, as `newtask:recipe` fills it: digest-folder, its input, its grant ticked, and the same run as typing. */
export const Recipe = () => (
  <NewTaskSheet
    model={useTasksPreview({
      then: async (place) => {
        await place.openNew()
        await place.pickRecipe(recipeFrame.recipe)
        place.setForm({ input: recipeFrame.input, grants: [...recipeFrame.grants] })
      },
    })}
  />
)

/** A recipe that takes input, none given: Start off. */
export const RecipeWithoutInput = () => (
  <NewTaskSheet
    model={useTasksPreview({
      then: async (place) => {
        await place.openNew()
        await place.pickRecipe('changelog-draft')
      },
    })}
  />
)

/** The refusal's alert, as `tasks:refused` raises it: the sheet's start refused, the CLI's line and OK alone. */
export const Refused = () => (
  <TaskRefusedAlert
    model={useTasksPreview({
      ticks: 1,
      then: async (place) => {
        await place.openNew()
        place.setForm({ goal: newTaskGoal, writes: 'allow', cmds: 'allow' })
        place.start()
      },
    })}
    ok={() => {}}
  />
)
