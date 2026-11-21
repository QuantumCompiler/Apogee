import { describe, expect, test } from 'vitest'
import mediaList from '@recorded/business/reads/Fixtures/media-list.json?raw'
import videoList from '../../../fixtures/business/reads/models-list-video-handmade.json?raw'
import { MediaEngine, MediaModel } from '../../../source/business/media/MediaEngine'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('not a document')
  return read
}

describe('what a clip runs on: the engine its model’s directory says (the Mac’s MediaEngine and MediaModel)', () => {
  test('videoModels(): media-list.json’s pipelines on PyTorch and its MLX video model on MLX, offered folder by folder, PyTorch first', () => {
    const models = MediaModel.videoModels(document(mediaList))
    expect(models.map((model) => `${model.handle} ${model.engine}`)).toEqual([
      'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6 torch',
      'Wan2.2-TI2V-5B/mlx/5e6f708192a3 mlx',
      'Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3 torch',
    ])
    expect(models.find((model) => model.handle.startsWith('Wan2.1'))?.bytes).toBe(462)
  })

  test('the video world’s list: the pipeline beside its MLX conversion, so the engine is the person’s choice there', () => {
    const models = MediaModel.videoModels(document(videoList))
    expect(models.map((model) => model.handle)).toEqual(['Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', 'Wan2.2-TI2V-5B/mlx/5e6f708192a3'])
    for (const model of models) expect(MediaModel.choosesEngine(model, models)).toBe(true)
    expect(models.map((model) => MediaModel.label(model, models))).toEqual(['Wan2.2-TI2V-5B · PyTorch', 'Wan2.2-TI2V-5B · MLX'])
  })

  test('named(): a handle, or a folder holding one; never a folder holding several', () => {
    const models = MediaModel.videoModels(document(mediaList))
    expect(MediaModel.named('Wan2.1-T2V-1.3B', models)?.handle).toBe('Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3')
    expect(MediaModel.named('Wan2.2-TI2V-5B', models)).toBeUndefined()
    expect(MediaModel.named('Wan2.2-TI2V-5B/mlx/5e6f708192a3', models)?.engine).toBe('mlx')
  })

  test('an engine as a person reads it, and a word this build does not know kept as written', () => {
    expect(MediaEngine.name('torch')).toBe('PyTorch')
    expect(MediaEngine.name('mlx')).toBe('MLX')
    expect(MediaEngine.name('onnx')).toBe('onnx')
    expect(MediaEngine.video('pipeline', undefined)).toBe('torch')
    expect(MediaEngine.video('mlx', 'video')).toBe('mlx')
    expect(MediaEngine.video('mlx', 'voice')).toBeUndefined()
  })
})
