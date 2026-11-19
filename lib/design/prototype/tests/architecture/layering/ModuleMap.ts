import type { SourceTree } from './SourceTree'

/** One module of the map: its id is its path under source/, `<layer>/<package>`. */
export interface Module {
  readonly id: string
  /** The ids of the modules it may import, as the map lists them. */
  readonly dependsOn: readonly string[]
}

/**
 * The prototype's module map, as `modules.json` declares it -- the one place
 * that says each module's layer and what it may depend on -- and the packages
 * `package.json` lists, the only things the prototype may take from outside.
 *
 * `modules.json` is `{ "modules": { "<layer>/<package>": ["<layer>/<package>", ...] } }`,
 * read in its own order.
 */
export class ModuleMap {
  constructor(
    readonly modules: readonly Module[],
    readonly dependencies: ReadonlySet<string>,
    readonly devDependencies: ReadonlySet<string>,
  ) {}

  /** The map the tree holds: its `modules.json` and `package.json`. */
  static read(tree: SourceTree): ModuleMap {
    const modules = tree.contents('modules.json')
    if (modules === undefined) throw new Error('modules.json is missing: the module map is the one declaration of every module')
    const packages = tree.contents('package.json')
    if (packages === undefined) throw new Error('package.json is missing: it lists every package the prototype may import')
    return ModuleMap.parse(modules, packages)
  }

  /** The map from the text of `modules.json` and `package.json`. */
  static parse(modulesJson: string, packageJson: string): ModuleMap {
    const declared = (JSON.parse(modulesJson) as { modules?: unknown }).modules ?? {}
    if (typeof declared !== 'object' || declared === null || Array.isArray(declared)) {
      throw new Error('modules.json\'s "modules" is not an object of module ids to the ids they depend on')
    }
    const modules = Object.entries(declared).map(([id, dependsOn]): Module => {
      if (!Array.isArray(dependsOn) || dependsOn.some((dependency) => typeof dependency !== 'string')) {
        throw new Error(`modules.json gives ${id} ${JSON.stringify(dependsOn)}: a module's entry is a list of module ids`)
      }
      return { id, dependsOn: dependsOn as string[] }
    })
    const manifest = JSON.parse(packageJson) as { dependencies?: object; devDependencies?: object }
    return new ModuleMap(
      modules,
      new Set(Object.keys(manifest.dependencies ?? {})),
      new Set(Object.keys(manifest.devDependencies ?? {})),
    )
  }
}
