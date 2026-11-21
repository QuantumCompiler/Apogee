/** Review's words (design 1n): the Mac app's where it has them, the old prototype's where it drew them. */

/** The words for no apogee, said in every tab (`ReviewViewModel.swift:215`, the old `rv.noBin` lines). */
export const missingWords = "Apogee isn't installed, so there is nothing to review with."

/** What the Skills tab's page says with no lens to show: the old prototype's words. */
export const noSkillWords = 'Every lens a review can run, shipped and your own. Choose one to read its prompt.'

/** The lens the Skills tab shows until another is chosen: the old prototype's `rv.skill`. */
export const defaultSkill = 'security'

/** The add-target sheet's starting fields, as the old prototype opens it (`rv.addTarget`, `main.html` at `b375fac9`): `gadget`, watching the repository `acme/gadget`. */
export const reviewStartingTarget = { name: 'gadget', repo: 'acme/gadget' } as const
