import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from 'react'

type Photo = { url: string; fileName: string }
type PhotoContextValue = { photos: ReadonlyMap<number, Photo>; setPhoto: (studentId: number, photo: Photo) => void; removePhoto: (studentId: number) => void }
const StudentPhotoContext = createContext<PhotoContextValue | null>(null)
const revokeObjectUrl = (url: string) => URL.revokeObjectURL(url)

export function StudentPhotoProvider({ children, revoke = revokeObjectUrl }: { children: ReactNode; revoke?: (url: string) => void }) {
  const [photos, setPhotos] = useState<Map<number, Photo>>(() => new Map())
  const latest = useRef(photos)
  latest.current = photos
  useEffect(() => () => { for (const photo of latest.current.values()) revoke(photo.url) }, [revoke])
  const setPhoto = (studentId: number, photo: Photo) => {
    const next = new Map(latest.current)
    const previous = next.get(studentId)
    if (previous) revoke(previous.url)
    next.set(studentId, photo)
    latest.current = next
    setPhotos(next)
  }
  const removePhoto = (studentId: number) => {
    const next = new Map(latest.current)
    const previous = next.get(studentId)
    if (previous) revoke(previous.url)
    next.delete(studentId)
    latest.current = next
    setPhotos(next)
  }
  return <StudentPhotoContext.Provider value={{ photos, setPhoto, removePhoto }}>{children}</StudentPhotoContext.Provider>
}

export function useStudentPhotos() {
  const value = useContext(StudentPhotoContext)
  if (!value) throw new Error('StudentPhotoProvider no está disponible.')
  return value
}
