import type { Student } from '../models/domain'
import { avatarColor, getStudentInitials } from '../services/studentDirectory'
import { useStudentPhotos } from '../students/StudentPhotoContext'

export function StudentAvatar({ student, size = 'regular' }: { student: Student; size?: 'regular' | 'large' }) {
  const { photos } = useStudentPhotos()
  const photo = photos.get(student.student_id)
  return <span className={`student-avatar student-avatar-${size}`} style={{ backgroundColor: avatarColor(student.student_id) }}>
    {photo ? <img src={photo.url} alt={`Imagen de demostración de ${student.name}`} /> : <span aria-label={`Iniciales de ${student.name}`}>{getStudentInitials(student)}</span>}
  </span>
}
