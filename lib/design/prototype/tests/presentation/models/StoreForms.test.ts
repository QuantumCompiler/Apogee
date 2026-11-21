import { describe, expect, test } from 'vitest'
import { ConvertForm, Estimate, QuantizeForm, RegisterForm, RenameForm, modelBaseName, stepLine, storeSize } from '../../../source/presentation/models/ViewModels/StoreForms'

const llamaSnapshot = 'Llama3.2-1B-Instruct/safetensors/edaba290ce7f'
/** store-info-snapshot-converts.json's `data.bytes`. */
const llamaBytes = 2482980600

describe('the store sheets’ forms: each the command a person would type (the Mac’s StoreForms, the prototype’s sheets)', () => {
  test('sizes in the CLI’s own units, truncated as it truncates; an estimate from bits per weight, said as one', () => {
    expect(storeSize(512)).toBe('512 B')
    expect(storeSize(2048)).toBe('2 KiB')
    expect(storeSize(770 * 1024 * 1024)).toBe('770 MiB')
    expect(storeSize(llamaBytes)).toBe('2.3 GiB')
    expect(Estimate.size('Q4_K_M', llamaBytes)).toBe('about 717 MiB')
    expect(Estimate.size('f16', undefined)).toBeUndefined()
    expect(modelBaseName('Wan-AI--Wan2.2-TI2V-5B')).toBe('Wan2.2-TI2V-5B')
    expect(modelBaseName('Llama3.2-1B-Instruct')).toBe('Llama3.2-1B-Instruct')
  })

  test('convert, as Convert to GGUF… opens it: f16, Q4_K_M, registered, named from the model -- `--register-with Q4_K_M`', () => {
    const form = ConvertForm.gguf(llamaSnapshot, llamaBytes)
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '--register-with', 'Q4_K_M'])
    expect(form.names).toEqual(['Llama3.2-1B-Instruct-F16', 'Llama3.2-1B-Instruct-Q4KM'])
    expect(form.steps.map(stepLine)).toEqual(['Convert to GGUF F16, about 2.3 GiB', 'Quantize to Q4_K_M, about 717 MiB', 'Register Llama3.2-1B-Instruct-F16, Llama3.2-1B-Instruct-Q4KM'])
    expect(form.registerForced).toBe(true)
    expect(form.errors([])).toEqual([])
    expect(form.command).toBe('apogee models convert Llama3.2-1B-Instruct/safetensors/edaba290ce7f --register-with Q4_K_M')
  })

  test('a taken backend name refuses, in the prototype’s words; so does an empty beginning', () => {
    const form = ConvertForm.gguf(llamaSnapshot, llamaBytes)
    expect(form.errors(['Llama3.2-1B-Instruct-Q4KM'])).toEqual(['A backend named Llama3.2-1B-Instruct-Q4KM exists: choose another beginning.'])
    expect(form.errors(['Llama3.2-1B-Instruct-F16', 'Llama3.2-1B-Instruct-Q4KM'])).toEqual(['Backends named Llama3.2-1B-Instruct-F16, Llama3.2-1B-Instruct-Q4KM exist: choose another beginning.'])
    form.baseName = '  '
    expect(form.errors([])).toEqual(['Name the backends: what each name begins with.'])
    form.baseName = 'llama'
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '--register-with', 'Q4_K_M', '--base-name', 'llama'])
  })

  test('no levels and no register: the F16 alone, nothing named; register alone: `--register`', () => {
    const form = ConvertForm.gguf(llamaSnapshot, llamaBytes)
    form.toggleLevel('Q4_K_M')
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '--register'])
    form.register = false
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot])
    expect(form.names).toEqual([])
  })

  test('a GGUF at another precision is made on its own: no levels, no register, the CLI’s reason said', () => {
    const form = ConvertForm.gguf(llamaSnapshot, llamaBytes)
    form.setPrecision('bf16')
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '-t', 'bf16'])
    expect(form.levels).toEqual([])
    expect(form.aloneNote).toBe('Registering and quantizing start from the F16, so a bf16 GGUF is made on its own. Choose f16 to register it or quantize from it.')
  })

  test('MLX: 4bit, registered; a further precision lists every one, the written first, as the CLI takes it', () => {
    const form = ConvertForm.mlx(llamaSnapshot, llamaBytes)
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '--mlx', '--register'])
    expect(form.levelChoices).toEqual(['3bit', '6bit', '8bit', 'mxfp4'])
    form.toggleLevel('8bit')
    expect(form.arguments).toEqual(['models', 'convert', llamaSnapshot, '--mlx', '--register-with', '4bit,8bit'])
    expect(form.names).toEqual(['Llama3.2-1B-Instruct-4bit', 'Llama3.2-1B-Instruct-8bit'])
    form.modelType = 'llama'
    expect(form.arguments.slice(-2)).toEqual(['--model-type', 'llama'])
  })

  test('quantize: Q4_K_M left out as the CLI’s default, the name following the level until typed, and the registration that follows', () => {
    const form = new QuantizeForm('Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41', 'Llama3.2-1B-Instruct', 2479596089)
    expect(form.arguments).toEqual(['models', 'quantize', 'Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41'])
    expect(form.name).toBe('Llama3.2-1B-Instruct-Q4KM')
    form.choose('Q8_0')
    expect(form.name).toBe('Llama3.2-1B-Instruct-Q80')
    expect(form.arguments).toEqual(['models', 'quantize', 'Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41', '-t', 'Q8_0'])
    form.rename('mine')
    form.choose('Q6_K')
    expect(form.name).toBe('mine')
    expect(form.registration('/f.gguf')).toEqual(['config', 'add-backend', 'mine', '--type', 'llamacpp', '--model-path', '/f.gguf'])
    expect(form.command).toBe('apogee models quantize Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41 -t Q6_K\nthen  apogee config add-backend mine --type llamacpp --model-path <the new file>')
    expect(form.errors(['mine'])).toEqual(['A backend named mine exists: choose another name.'])
    form.rename('')
    expect(form.errors([])).toEqual(['Name the backend.'])
    form.register = false
    expect(form.errors([])).toEqual([])
    expect(form.steps.map(stepLine)).toEqual(['Quantize to Q6_K, about 969 MiB'])
  })

  test('register: `config add-backend` with the path, a window and an attention cache only when chosen; a taken name refuses', () => {
    const form = new RegisterForm('Gemma4-26B-Instruct/mlx/a88b9f386aff', 'mlx', '/Users/someone/.apogee/models/Gemma4-26B-Instruct/mlx/a88b9f386aff', 'Gemma4-26B-Instruct-mixed_4_8', undefined, 32768)
    expect(form.arguments).toEqual(['config', 'add-backend', 'Gemma4-26B-Instruct-mixed_4_8', '--type', 'mlx', '--model-path', '/Users/someone/.apogee/models/Gemma4-26B-Instruct/mlx/a88b9f386aff'])
    form.window = '8192'
    form.cache = 'f16'
    expect(form.arguments.slice(-2), 'an mlx entry takes no cache type').toEqual(['--context-size', '8192'])
    expect(form.windowChoices[0]).toEqual({ value: '', label: 'the model’s default, 32,768 here' })
    expect(form.errors(['Gemma4-26B-Instruct-mixed_4_8'])).toEqual(['A backend named Gemma4-26B-Instruct-mixed_4_8 exists: choose another name.'])
    const gguf = new RegisterForm('x/gguf/1', 'llamacpp', '/x.gguf', 'x', '/x-mmproj.gguf', undefined)
    gguf.cache = 'q4_0'
    expect(gguf.arguments).toEqual(['config', 'add-backend', 'x', '--type', 'llamacpp', '--model-path', '/x.gguf', '--mmproj-path', '/x-mmproj.gguf', '--cache-type', 'q4_0'])
    expect(gguf.windowChoices[0]?.label).toBe('the model’s default')
    gguf.name = ' '
    expect(gguf.errors([])).toEqual(['Name the backend.'])
  })

  test('rename: `models rename`; the backends named after it follow; an empty, taken or spaced name refuses, and an unchanged one does nothing', () => {
    const form = new RenameForm('Llama3.2-1B-Instruct', 3, ['Llama3.2-1B-Instruct-F16', 'llama3.2-1b-Q4KM'])
    expect(form.canRename([])).toBe(false)
    expect(form.follow).toEqual(['Llama3.2-1B-Instruct-F16'])
    form.name = 'Llama-1B'
    expect(form.arguments).toEqual(['models', 'rename', 'Llama3.2-1B-Instruct', 'Llama-1B'])
    expect(form.errors(['Llama-1B'])).toEqual(['A model named Llama-1B is in the store already.'])
    form.name = 'a b'
    expect(form.errors([])).toEqual(['A name has no spaces or slashes.'])
    form.name = ''
    expect(form.errors([])).toEqual(['Give it a name.'])
  })
})
