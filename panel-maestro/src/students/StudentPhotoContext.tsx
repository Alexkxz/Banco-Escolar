import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from 'react'
import { loadPersistedStudentImages, persistStudentImage, removePersistedStudentImage } from '../services/demoPanelService'

type Photo = { url: string; fileName: string }
type PhotoContextValue = { photos: ReadonlyMap<number, Photo>; status: 'loading' | 'ready' | 'error'; error: string; setPhoto: (studentId: number, blob: Blob, fileName: string, dimensions: { width: number; height: number }) => Promise<void>; removePhoto: (studentId: number) => Promise<void> }
type PhotoStorage = { load: typeof loadPersistedStudentImages; save: typeof persistStudentImage; remove: typeof removePersistedStudentImage }
const StudentPhotoContext = createContext<PhotoContextValue | null>(null)
const revokeObjectUrl = (url: string) => URL.revokeObjectURL(url)
const createObjectUrl = (blob: Blob) => URL.createObjectURL(blob)
const defaultPhotoStorage: PhotoStorage = { load: loadPersistedStudentImages, save: persistStudentImage, remove: removePersistedStudentImage }

export function StudentPhotoProvider({ children, revoke = revokeObjectUrl, createUrl = createObjectUrl, storage = defaultPhotoStorage }: { children: ReactNode; revoke?: (url: string) => void; createUrl?: (blob: Blob) => string; storage?: PhotoStorage }) {
  const [photos, setPhotos] = useState<Map<number, Photo>>(() => new Map())
  const [status, setStatus] = useState<'loading' | 'ready' | 'error'>('loading')
  const [error, setError] = useState('')
  const latest = useRef(photos)
  latest.current = photos
  useEffect(() => {
    let active = true
    storage.load().then((images) => {
      if (!active) return
      const next = new Map<number, Photo>()
      for (const image of images) {
        if (image.blob instanceof Blob && image.blob.type === image.mimeType) next.set(image.student_id, { url: createUrl(image.blob), fileName: image.fileName })
      }
      latest.current = next; setPhotos(next); setStatus('ready')
    }).catch((reason: unknown) => { if (active) { setError(reason instanceof Error ? reason.message : 'No se pudieron cargar las imágenes locales.'); setStatus('error') } })
    return () => { active = false; for (const photo of latest.current.values()) revoke(photo.url) }
  }, [revoke, createUrl, storage])
  const setPhoto = async (studentId: number, blob: Blob, fileName: string, dimensions: { width: number; height: number }) => {
    const mime = blob.type
    await storage.save(studentId, blob, mime, fileName, dimensions)
    const next = new Map(latest.current)
    const previous = next.get(studentId)
    if (previous) revoke(previous.url)
    next.set(studentId, { url: createUrl(blob), fileName })
    latest.current = next
    setPhotos(next)
  }
  const removePhoto = async (studentId: number) => {
    await storage.remove(studentId)
    const next = new Map(latest.current)
    const previous = next.get(studentId)
    if (previous) revoke(previous.url)
    next.delete(studentId)
    latest.current = next
    setPhotos(next)
  }
  return <StudentPhotoContext.Provider value={{ photos, status, error, setPhoto, removePhoto }}>{children}</StudentPhotoContext.Provider>
}

export function useStudentPhotos() {
  const value = useContext(StudentPhotoContext)
  if (!value) throw new Error('StudentPhotoProvider no está disponible.')
  return value
}
