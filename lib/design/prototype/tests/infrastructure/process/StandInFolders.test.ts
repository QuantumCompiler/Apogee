import { describe, expect, test } from 'vitest'
import { StandInFolders } from '../../../source/infrastructure/process/StandInFolders'

describe('the folders the CLI wrote, as the stand-in answers them (design 1k)', () => {
  test('a folder lists its files as the preset holds them -- a clip by its name, its record with its text -- and a folder nobody named is empty', () => {
    const folders = new StandInFolders({ folders: { '/m': [{ name: 'a.mp4' }, { name: 'a.mp4.json', text: '{}' }] }, apogee: 'installed' })
    expect(folders.files('/m')).toEqual([{ name: 'a.mp4' }, { name: 'a.mp4.json', text: '{}' }])
    expect(folders.files('/elsewhere')).toEqual([])
  })

  test('the folders are the person’s files, not the CLI’s: they read the same whether apogee is installed or not', () => {
    const folders = new StandInFolders({ folders: { '/m': [{ name: 'a.mp4' }] }, apogee: 'missing' })
    expect(folders.files('/m')).toEqual([{ name: 'a.mp4' }])
    expect(new StandInFolders({ apogee: 'installed' }).files('/m')).toEqual([])
  })
})
