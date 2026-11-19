/**
 * The application's view-model: the composition root, where the running app's
 * stand-ins and each place's view-model are built (ADR 0001 §3). Empty until
 * 1d's stand-in and 1e's shell arrive: the app is a window and its name.
 */
export class AppViewModel {
  readonly name = 'Apogee'
}
