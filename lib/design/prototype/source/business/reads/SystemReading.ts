import { JSONValue } from '../../data/protocol/JSONValue'
import type { JSONObject } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * `system --output-format json`: the machine as the CLI reads it -- the Mac
 * app's `SystemReading` (`BusinessReads`, `System.swift`), with the fields the
 * design's monitor draws besides (design 1e): the memory available, the
 * models held, the GPU's and the budget's reasons when unknown, and the
 * store's path. Every field the CLI prints is always there, `null` where the
 * platform cannot read it (`machine-mode.md`, `system`); here that is
 * `undefined`, never a number made up.
 */
export class SystemReading {
  readonly cpuModel: string
  /** The CPU's utilization over the read's window, 0-100. */
  readonly cpuPercent: number | undefined
  readonly memoryTotal: number | undefined
  readonly memoryUsed: number | undefined
  readonly memoryAvailable: number | undefined
  /** What apogee's own reading process holds. */
  readonly apogeeBytes: number | undefined
  /** The backends whose models the reading process holds, by name: the CLI gives no size for them. */
  readonly modelsHeld: readonly string[]
  readonly gpuName: string | undefined
  /** Why the GPU is unknown, in the CLI's words. */
  readonly gpuUnknown: string | undefined
  /** What models may offload to. */
  readonly modelBudget: number | undefined
  /** Why the budget is unknown, in the CLI's words. */
  readonly modelBudgetUnknown: string | undefined
  /** The model store's folder. */
  readonly store: string | undefined
  readonly storeBytes: number | undefined
  readonly diskAvailable: number | undefined
  readonly diskCapacity: number | undefined

  constructor(document: ReadDocument) {
    const section = (name: string): JSONObject => JSONValue.object(document.value[name]) ?? {}
    const cpu = section('cpu')
    const memory = section('memory')
    const apogee = section('apogee')
    const gpu = section('gpu')
    const disk = section('disk')
    this.cpuModel = JSONValue.string(cpu.model) ?? ''
    this.cpuPercent = JSONValue.number(JSONValue.object(cpu.utilization)?.percent)
    this.memoryTotal = JSONValue.int(memory.total_bytes)
    this.memoryUsed = JSONValue.int(memory.used_bytes)
    this.memoryAvailable = JSONValue.int(memory.available_bytes)
    this.apogeeBytes = JSONValue.int(apogee.process_bytes)
    this.modelsHeld = (JSONValue.array(apogee.models_held) ?? []).flatMap((name) => (typeof name === 'string' ? [name] : []))
    this.gpuName = JSONValue.string(gpu.name)
    this.gpuUnknown = JSONValue.string(gpu.unknown)
    this.modelBudget = JSONValue.int(gpu.model_budget_bytes)
    this.modelBudgetUnknown = JSONValue.string(gpu.model_budget_unknown)
    this.store = JSONValue.string(disk.store)
    this.storeBytes = JSONValue.int(disk.store_bytes)
    this.diskAvailable = JSONValue.int(disk.volume_available_bytes)
    this.diskCapacity = JSONValue.int(disk.volume_capacity_bytes)
  }
}
